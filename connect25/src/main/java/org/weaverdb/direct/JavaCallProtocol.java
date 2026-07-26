package org.weaverdb.direct;

import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;

/**
 * Defines the binary protocol used for FFM upcall-based Java function invocation
 * (replacing the old JNI path for LANGUAGE 'java' functions).
 *
 * This is the contract between the native WeaverDB engine (C) and the Java invoker.
 *
 * Versioning: The first 4 bytes of the argBlock can eventually carry a protocol version.
 * For v1 we keep things extremely simple.
 */
public final class JavaCallProtocol {

    private JavaCallProtocol() {}

    // ====================== Type Tags (match Weaver type Oids where sensible) ======================

    public static final int TAG_INT4       = 23;     // int
    public static final int TAG_INT8       = 20;     // long
    public static final int TAG_FLOAT8     = 701;    // double
    public static final int TAG_BOOL       = 16;
    public static final int TAG_VARCHAR    = 1043;
    public static final int TAG_JAVA_OBJECT= 1830;   // complex Java object (serialized via WeaverObjectLoader style)

    // Special internal tags
    public static final int TAG_NULL       = -1;
    public static final int TAG_ERROR      = -99;   // Error reported from Java (message in payload)

    // ====================== Arg Block Format (v1) ======================
    //
    // Layout:
    //   [int32 numArgs]
    //   for each arg:
    //       [int32 typeTag]
    //       [int32 lengthInBytes]     // for primitives usually 4/8, for JAVA_OBJECT = serialized length
    //       [bytes data]
    //
    // Strings (VARCHAR) are sent as UTF-8 bytes (no trailing null required).
    // JAVA_OBJECT uses the same serialization format as WeaverObjectLoader.java_in / java_out.
    //
    // The result block uses the exact same format but for exactly one value (numArgs is always 1 or 0 for void).

    public static final int HEADER_SIZE = 4; // numArgs

    public static int writeInt4(MemorySegment buf, long offset, int value) {
        buf.set(ValueLayout.JAVA_INT, offset, value);
        return 4;
    }

    public static int writeInt8(MemorySegment buf, long offset, long value) {
        buf.set(ValueLayout.JAVA_LONG, offset, value);
        return 8;
    }

    public static int writeFloat8(MemorySegment buf, long offset, double value) {
        buf.set(ValueLayout.JAVA_DOUBLE, offset, value);
        return 8;
    }

    public static int writeBool(MemorySegment buf, long offset, boolean value) {
        buf.set(ValueLayout.JAVA_BOOLEAN, offset, value);
        return 1;
    }

    public static int writeBytes(MemorySegment buf, long offset, byte[] data) {
        for (int i = 0; i < data.length; i++) {
            buf.set(ValueLayout.JAVA_BYTE, offset + i, data[i]);
        }
        return data.length;
    }

    public static int writeString(MemorySegment buf, long offset, String s) {
        byte[] bytes = s.getBytes(StandardCharsets.UTF_8);
        return writeBytes(buf, offset, bytes);
    }

    // ====================== Simple Reader Helpers ======================

    public static int readInt4(MemorySegment buf, long offset) {
        return buf.get(ValueLayout.JAVA_INT, offset);
    }

    public static long readInt8(MemorySegment buf, long offset) {
        return buf.get(ValueLayout.JAVA_LONG, offset);
    }

    public static double readFloat8(MemorySegment buf, long offset) {
        return buf.get(ValueLayout.JAVA_DOUBLE, offset);
    }

    public static boolean readBool(MemorySegment buf, long offset) {
        return buf.get(ValueLayout.JAVA_BOOLEAN, offset);
    }

    public static byte[] readBytes(MemorySegment buf, long offset, int length) {
        byte[] out = new byte[length];
        for (int i = 0; i < length; i++) {
            out[i] = buf.get(ValueLayout.JAVA_BYTE, offset + i);
        }
        return out;
    }

    public static String readString(MemorySegment buf, long offset, int length) {
        byte[] bytes = readBytes(buf, offset, length);
        return new String(bytes, StandardCharsets.UTF_8);
    }

    /**
     * Very small helper to build an argument block.
     * Not meant to be highly optimized yet.
     */
    public static final class ArgBlockBuilder {
        private final byte[] buffer;
        private int pos = 4; // leave room for numArgs
        private int count = 0;

        public ArgBlockBuilder(int estimatedSize) {
            this.buffer = new byte[estimatedSize];
        }

        public ArgBlockBuilder addInt4(int v) {
            ensureCapacity(pos + 12);
            writeInt4At(pos, TAG_INT4); pos += 4;
            writeInt4At(pos, 4);        pos += 4;
            writeInt4At(pos, v);        pos += 4;
            count++;
            return this;
        }

        public ArgBlockBuilder addInt8(long v) {
            ensureCapacity(pos + 16);
            writeInt4At(pos, TAG_INT8); pos += 4;
            writeInt4At(pos, 8);        pos += 4;
            writeLongAt(pos, v);        pos += 8;
            count++;
            return this;
        }

        public ArgBlockBuilder addFloat8(double v) {
            ensureCapacity(pos + 16);
            writeInt4At(pos, TAG_FLOAT8); pos += 4;
            writeInt4At(pos, 8);          pos += 4;
            writeDoubleAt(pos, v);        pos += 8;
            count++;
            return this;
        }

        public ArgBlockBuilder addBool(boolean v) {
            ensureCapacity(pos + 9);
            writeInt4At(pos, TAG_BOOL); pos += 4;
            writeInt4At(pos, 1);        pos += 4;
            buffer[pos++] = (byte) (v ? 1 : 0);
            count++;
            return this;
        }

        public ArgBlockBuilder addJavaObject(byte[] serialized) {
            ensureCapacity(pos + 8 + serialized.length);
            writeInt4At(pos, TAG_JAVA_OBJECT); pos += 4;
            writeInt4At(pos, serialized.length); pos += 4;
            System.arraycopy(serialized, 0, buffer, pos, serialized.length);
            pos += serialized.length;
            count++;
            return this;
        }

        public byte[] build() {
            writeInt4At(0, count);
            byte[] result = new byte[pos];
            System.arraycopy(buffer, 0, result, 0, pos);
            return result;
        }

        private void ensureCapacity(int needed) {
            if (needed > buffer.length) {
                throw new IllegalStateException("ArgBlockBuilder too small, increase estimated size");
            }
        }

        private void writeInt4At(int off, int v) {
            buffer[off]     = (byte) (v);
            buffer[off + 1] = (byte) (v >>> 8);
            buffer[off + 2] = (byte) (v >>> 16);
            buffer[off + 3] = (byte) (v >>> 24);
        }

        private void writeLongAt(int off, long v) {
            for (int i = 0; i < 8; i++) {
                buffer[off + i] = (byte) (v >>> (i * 8));
            }
        }

        private void writeDoubleAt(int off, double v) {
            long bits = Double.doubleToRawLongBits(v);
            writeLongAt(off, bits);
        }
    }

    // =====================================================================
    // Error support for better exception propagation from Java to C
    // =====================================================================

    /**
     * Writes an error result into the given buffer using the protocol.
     * The C side will turn this into a proper elog(ERROR).
     */
    public static void writeError(MemorySegment resultOut, String message) {
        if (message == null) message = "Unknown Java error";

        byte[] msgBytes = message.getBytes(java.nio.charset.StandardCharsets.UTF_8);

        // Write protocol header manually (avoiding removed helper)
        resultOut.set(java.lang.foreign.ValueLayout.JAVA_INT, 0, 1); // num values = 1
        long off = 4;
        resultOut.set(java.lang.foreign.ValueLayout.JAVA_INT, off, TAG_ERROR); off += 4;
        resultOut.set(java.lang.foreign.ValueLayout.JAVA_INT, off, msgBytes.length); off += 4;

        for (int i = 0; i < msgBytes.length; i++) {
            resultOut.set(java.lang.foreign.ValueLayout.JAVA_BYTE, off + i, msgBytes[i]);
        }
    }

}
