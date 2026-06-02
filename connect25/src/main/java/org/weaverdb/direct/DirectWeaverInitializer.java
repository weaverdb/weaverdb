/*-------------------------------------------------------------------------
 *
 *
 * Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 *
 * All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.direct;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.time.Duration;
import java.time.Instant;
import java.util.Properties;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import org.weaverdb.DBReference;
import org.weaverdb.DBReferenceManager;

/**
 * Modern FFM-based initializer for WeaverDB.
 *
 * LONG-TERM STRATEGY (recommended path):
 *   - Use DirectWeaverInitializer + the FFM client (DirectWeaverConnection etc.)
 *     for all new code.
 *   - This path enables Java stored procedures (LANGUAGE 'java') via pure FFM
 *     upcalls instead of classic JNI.
 *   - The older WeaverInitializer + weaver_jni library is retained for
 *     backward compatibility only.
 */
public class DirectWeaverInitializer {
    
    private static boolean loaded = false;
    
    private static final MethodHandle initWeaverBackend;
    private static final MethodHandle wrapupWeaverBackend;
    private static final MethodHandle registerJavaInvoker;

    // Strong reference to the JavaFunctionInvoker so the upcall stub (registered
    // via Arena.global()) remains reachable for the lifetime of the process.
    // This is critical for thread safety and lifetime correctness of FFM upcalls.
    private static volatile JavaFunctionInvoker javaFunctionInvoker;

    static {
        System.loadLibrary("weaver");
        Linker linker = Linker.nativeLinker();
        SymbolLookup lookup = SymbolLookup.loaderLookup();
        
        initWeaverBackend = linker.downcallHandle(
            lookup.find("initweaverbackend").orElseThrow(),
            FunctionDescriptor.of(ValueLayout.JAVA_BOOLEAN, ValueLayout.ADDRESS)
        );
        
        wrapupWeaverBackend = linker.downcallHandle(
            lookup.find("wrapupweaverbackend").orElseThrow(),
            FunctionDescriptor.ofVoid()
        );

        // New FFM path for Java function (LANGUAGE 'java') support via upcall invoker
        registerJavaInvoker = linker.downcallHandle(
            lookup.find("WRegisterJavaFunctionInvoker").orElseThrow(),
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS)
        );
    }
    
    public DirectWeaverInitializer() {
        
    }
    
    private static synchronized void init(String database) {
        try (Arena arena = Arena.ofConfined()) {
            boolean success = (boolean) initWeaverBackend.invokeExact(arena.allocateFrom(database));
            if (!success) {
                throw new UnsatisfiedLinkError("environment not valid, see db log");
            }

            // Register FFM upcall-based Java function invoker.
            // We keep a strong reference to the invoker instance so the upcall
            // stub (created with Arena.global()) stays valid for the life of the process.
            // This is essential for correct FFM upcall semantics when called from
            // native database threads.
            try {
                javaFunctionInvoker = new JavaFunctionInvoker();
                MemorySegment stub = javaFunctionInvoker.createUpcallStub();
                registerJavaInvoker.invokeExact(stub);
            } catch (Throwable t) {
                System.err.println("Warning: Failed to register Java function invoker via FFM. " +
                                   "LANGUAGE 'java' functions may fall back to legacy JNI or be unavailable: " + t);
                javaFunctionInvoker = null;
            }
        } catch (Throwable e) {
            throw new RuntimeException(e);
        }
    }

    private static synchronized void close() {
        try {
            wrapupWeaverBackend.invokeExact();
        } catch (Throwable e) {
            throw new RuntimeException(e);
        }
    }
    
    public static void initialize(Properties props) throws java.lang.UnsatisfiedLinkError  {
        StringBuilder vars = new StringBuilder();
        
        if ( loaded ) return;
        
        java.util.Enumeration it = props.keys();
        while ( it.hasMoreElements() ) {
            String key = it.nextElement().toString();
            vars.append(key).append("=").append(props.getProperty(key)).append(";");
        }
        
        init(vars.toString());
        
        loaded = true;
    }
    
    public static void shutdown(Duration timeout) throws TimeoutException {
        Instant start = Instant.now();
        boolean wasInterruped = false;

        try {
            while (DBReferenceManager.hasLiveConnections()&& start.plus(timeout).isAfter(Instant.now())) {
                TimeUnit.SECONDS.sleep(1);
            }
            if (DBReferenceManager.hasLiveConnections()) {
                throw new TimeoutException("close timeout exceeded.  Live connections still active");
            } else {
                close();
            }
        } catch (InterruptedException ie) {
            wasInterruped = true;
        } finally {
            if (wasInterruped) {
                Thread.currentThread().interrupt();
            }            
        }
    }
    
    public static void forceShutdown() {
        close();
    }

    /**
     * Returns the JavaFunctionInvoker currently registered for FFM upcalls, if any.
     * This can be useful for diagnostics or advanced integration.
     *
     * Thread-safety note: The returned instance is intended to be called from
     * native database threads via the upcall mechanism. The implementation uses
     * a ConcurrentHashMap for MethodHandle caching and is otherwise stateless
     * per invocation.
     */
    public static JavaFunctionInvoker getJavaFunctionInvoker() {
        return javaFunctionInvoker;
    }
}
