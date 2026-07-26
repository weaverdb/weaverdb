package org.weaverdb.direct;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.concurrent.ConcurrentHashMap;
import org.weaverdb.WeaverObjectLoader;
import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_BOOLEAN;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_DOUBLE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

/**
 * FFM-based invoker for WeaverDB Java stored procedures / functions (LANGUAGE 'java').
 *
 * LONG-TERM STRATEGY:
 *   This is the primary mechanism for Java function execution when using the
 *   modern FFM client (DirectWeaverInitializer). The legacy JNI path in the
 *   C backend is retained only for backward compatibility.
 *
 * THREAD SAFETY & ARENA LIFETIME NOTES (critical for upcalls):
 *
 * - This object is called via FFM upcall from native database threads.
 *   These threads are not Java threads initially; the JVM attaches them as needed.
 *
 * - The upcall stub is created with Arena.global() (see createUpcallStub) to
 *   ensure it remains valid for the entire process lifetime.
 *
 * - Concurrent calls from multiple database threads are possible and expected.
 *   The MethodHandle cache uses ConcurrentHashMap. The rest of the invoke path
 *   is stateless per call (except for object serialization via WeaverObjectLoader).
 *
 * - Do not capture or close the Arena used for the upcall stub yourself.
 *
 * This class is responsible for:
 * - Receiving upcalls from the native engine when a Java function needs to be executed.
 * - Caching MethodHandles for target methods for performance.
 * - Performing the actual invocation using MethodHandles (no reflection after initial lookup).
 *
 * The native side (via FFM upcall stub) will call into the method exposed by createUpcallStub().
 */
public final class JavaFunctionInvoker {

    private static final Linker LINKER = Linker.nativeLinker();

    // Cache: key = "className.methodName:descriptor"
    private final ConcurrentHashMap<String, MethodHandle> methodCache = new ConcurrentHashMap<>();

    private final MethodHandles.Lookup lookup = MethodHandles.lookup();

    /**
     * === Java Call Protocol v1 (defined in JavaCallProtocol) ===
     *
     * Upcall signature (seen by native code):
     *   int invoke(
     *       MemorySegment classNameUtf8,
     *       MemorySegment methodNameUtf8,
     *       MemorySegment methodDescriptorUtf8,
     *       int isStatic,
     *       MemorySegment argData,
     *       int argDataLen,
     *       MemorySegment resultOut
     *   )
     *
     * See JavaCallProtocol.java for the exact binary layout.
     *
     * Current status (checkpoint):
     * - Protocol definition + Java-side parsing/writing is implemented.
     * - C side now has full bridge: BuildArgBlockForInvoker + InvokeJavaViaFFMInvoker + ParseInvokerResult.
     * - CallJavaFunction prefers FFM upcall path when registered.
     * - Strings (class/method/desc) are now stored in FuncDef for the invoker.
     * - Result parsing (including JAVA_OBJECT via javaout) is implemented on C side.
     */
    private int invoke(
            MemorySegment className,      // const char* (null-terminated UTF-8)
            MemorySegment methodName,     // const char* (null-terminated UTF-8)
            MemorySegment methodDesc,     // const char* (null-terminated UTF-8)
            int isStaticFlag,
            MemorySegment argData,        // const char* raw arg block
            int argDataLen,
            MemorySegment resultOut       // char* writable result buffer
    ) {
        try {
            String classStr = className.getString(0);
            String methodStr = methodName.getString(0);
            String descStr = methodDesc.getString(0);

            String cacheKey = classStr + "." + methodStr + ":" + descStr;

            MethodHandle mh = methodCache.computeIfAbsent(cacheKey, key -> {
                try {
                    Class<?> clazz = Class.forName(classStr);
                    MethodType fullMt = MethodType.fromMethodDescriptorString(descStr, clazz.getClassLoader());

                    if (isStaticFlag == 1) {
                        return lookup.findStatic(clazz, methodStr, fullMt);
                    } else {
                        // For instance methods, the descriptor from the catalog includes the receiver
                        // as the first parameter (thanks to FunctionInstaller). We must drop it.
                        MethodType realMt = fullMt.dropParameterTypes(0, 1);
                        return lookup.findVirtual(clazz, methodStr, realMt);
                    }
                } catch (Exception e) {
                    throw new RuntimeException("Failed to resolve Java function handle: " + key, e);
                }
            });

            // Always parse the actual number of arguments sent from C (includes receiver for instance methods)
            int numArgsFromProtocol = JavaCallProtocol.readInt4(argData, 0);
            Object[] allArgs = parseArguments(argData, argDataLen, numArgsFromProtocol);

            Object result;
            if (isStaticFlag == 1) {
                result = mh.invokeWithArguments(allArgs);
            } else {
                if (allArgs.length == 0) {
                    throw new IllegalStateException("Instance method called with no receiver argument");
                }
                Object receiver = allArgs[0];
                Object[] methodArgs = (allArgs.length > 1)
                        ? java.util.Arrays.copyOfRange(allArgs, 1, allArgs.length)
                        : new Object[0];
                result = mh.bindTo(receiver).invokeWithArguments(methodArgs);
            }

            // Write result back using the same protocol (single value)
            writeResult(resultOut, result);

            return 0; // success

        } catch (Throwable t) {
            // Report the error back through the result buffer using the protocol.
            // The C side will turn this into a proper database error.
            String msg = t.getClass().getSimpleName() + ": " + t.getMessage();
            if (t.getCause() != null) {
                msg += " (caused by " + t.getCause().getClass().getSimpleName() + ")";
            }
            JavaCallProtocol.writeError(resultOut, msg);

            // Still return non-zero so C knows something went wrong,
            // but the detailed message is in the result buffer.
            return -1;
        }
    }

    /**
     * Parses arguments from the protocol block.
     * @param argData        the serialized argument data (per JavaCallProtocol)
     * @param len            length of the block
     * @param maxToParse     maximum number of arguments to parse (use 0 or negative for "all available")
     */
    private Object[] parseArguments(MemorySegment argData, int len, int maxToParse) {
        if (len < 4) return new Object[0];

        int numArgs = JavaCallProtocol.readInt4(argData, 0);
        int toParse = (maxToParse > 0) ? Math.min(numArgs, maxToParse) : numArgs;
        Object[] args = new Object[toParse];
        long offset = 4;

        for (int i = 0; i < args.length; i++) {
            if (offset + 8 > len) break;

            int tag = JavaCallProtocol.readInt4(argData, offset); offset += 4;
            int valueLen = JavaCallProtocol.readInt4(argData, offset); offset += 4;

            switch (tag) {
                case JavaCallProtocol.TAG_INT4:
                    args[i] = JavaCallProtocol.readInt4(argData, offset);
                    offset += 4;
                    break;
                case JavaCallProtocol.TAG_INT8:
                    args[i] = JavaCallProtocol.readInt8(argData, offset);
                    offset += 8;
                    break;
                case JavaCallProtocol.TAG_FLOAT8:
                    args[i] = JavaCallProtocol.readFloat8(argData, offset);
                    offset += 8;
                    break;
                case JavaCallProtocol.TAG_BOOL:
                    args[i] = JavaCallProtocol.readBool(argData, offset);
                    offset += 1;
                    break;
                case JavaCallProtocol.TAG_JAVA_OBJECT:
                    byte[] serialized = JavaCallProtocol.readBytes(argData, offset, valueLen);
                    args[i] = WeaverObjectLoader.java_out(serialized);
                    offset += valueLen;
                    break;
                case JavaCallProtocol.TAG_NULL:
                    args[i] = null;
                    break;
                default:
                    offset += valueLen;
                    args[i] = null;
            }
        }
        return args;
    }

    private void writeResult(MemorySegment resultOut, Object result) {
        if (result == null) {
            JavaCallProtocol.writeInt4(resultOut, 0, 1); // num values = 1
            // write null tag
            long off = 4;
            JavaCallProtocol.writeInt4(resultOut, off, JavaCallProtocol.TAG_NULL); off += 4;
            JavaCallProtocol.writeInt4(resultOut, off, 0);
            return;
        }

        JavaCallProtocol.writeInt4(resultOut, 0, 1);

        long off = 4;
        if (result instanceof Integer) {
            JavaCallProtocol.writeInt4(resultOut, off, JavaCallProtocol.TAG_INT4); off += 4;
            JavaCallProtocol.writeInt4(resultOut, off, 4); off += 4;
            JavaCallProtocol.writeInt4(resultOut, off, (Integer) result);
        } else if (result instanceof Long) {
            JavaCallProtocol.writeInt4(resultOut, off, JavaCallProtocol.TAG_INT8); off += 4;
            JavaCallProtocol.writeInt4(resultOut, off, 8); off += 4;
            resultOut.set(JAVA_LONG, off, (Long) result);
        } else if (result instanceof Double) {
            JavaCallProtocol.writeInt4(resultOut, off, JavaCallProtocol.TAG_FLOAT8); off += 4;
            JavaCallProtocol.writeInt4(resultOut, off, 8); off += 4;
            resultOut.set(JAVA_DOUBLE, off, (Double) result);
        } else if (result instanceof Boolean) {
            JavaCallProtocol.writeInt4(resultOut, off, JavaCallProtocol.TAG_BOOL); off += 4;
            JavaCallProtocol.writeInt4(resultOut, off, 1); off += 4;
            resultOut.set(JAVA_BOOLEAN, off, (Boolean) result);
        } else {
            // Complex object -> serialize using existing mechanism
            byte[] serialized = WeaverObjectLoader.java_in(result);
            JavaCallProtocol.writeInt4(resultOut, off, JavaCallProtocol.TAG_JAVA_OBJECT); off += 4;
            JavaCallProtocol.writeInt4(resultOut, off, serialized.length); off += 4;
            for (int i = 0; i < serialized.length; i++) {
                resultOut.set(JAVA_BYTE, off + i, serialized[i]);
            }
        }
    }

    /**
     * Creates the upcall stub that can be passed to native code via WRegisterJavaFunctionInvoker.
     *
     * The resulting function pointer must match this exact C signature:
     *
     *   int invoker(
     *       const char *className,   // null-terminated UTF-8
     *       const char *methodName,
     *       const char *methodDesc,
     *       int         isStatic,    // 1 = static, 0 = instance
     *       const char *argData,     // binary block per JavaCallProtocol v1
     *       int         argDataLen,
     *       char       *resultOut    // writable buffer
     *   );
     *
     * See also: JavaCallProtocol.java for the arg/result block layout.
     */
    public MemorySegment createUpcallStub() {
        MethodHandle target;
        try {
            target = MethodHandles.lookup()
                    .findVirtual(JavaFunctionInvoker.class, "invoke",
                            MethodType.methodType(int.class,
                                    MemorySegment.class, MemorySegment.class, MemorySegment.class,
                                    int.class, MemorySegment.class, int.class, MemorySegment.class))
                    .bindTo(this);
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new RuntimeException("Failed to create invoker upcall", e);
        }

        FunctionDescriptor desc = FunctionDescriptor.of(
                JAVA_INT,
                ADDRESS,   // className   (const char*)
                ADDRESS,   // methodName  (const char*)
                ADDRESS,   // methodDesc  (const char*)
                JAVA_INT,  // isStatic
                ADDRESS,   // argData     (const char* raw argument block per JavaCallProtocol)
                JAVA_INT,  // argDataLen
                ADDRESS    // resultOut   (char* writable buffer for result per JavaCallProtocol)
        );

        // IMPORTANT: We use Arena.global() here because this upcall stub must remain
        // valid for the entire lifetime of the database process and can be invoked
        // at any time from native database threads (which may be different from
        // Java threads).
        //
        // Arena.ofAuto() is NOT safe for long-lived upcalls — the arena (and thus
        // the validity of the stub) can be collected by the GC at unpredictable times.
        //
        // Arena.global() ensures the stub lives until the JVM shuts down, which
        // matches the required lifetime for a database embedded in a long-running
        // Java process.
        return LINKER.upcallStub(target, desc, Arena.global());
    }

    // Convenience for testing / future direct Java usage
    public static JavaFunctionInvoker getDefault() {
        return new JavaFunctionInvoker();
    }
}
