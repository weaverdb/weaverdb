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

import java.io.Serializable;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.channels.ReadableByteChannel;
import java.nio.channels.WritableByteChannel;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import org.weaverdb.WeaverObjectLoader;
import java.util.Date;
import java.util.Map;
import org.weaverdb.ExecutionException;

/**
 *
 */
enum TransferType {
    BOOLEAN {
        @Override
        int getType() {
            return DirectWeaverConnection.BOOLEAN;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case DirectWeaverConnection.BYTE:
                    return val.get(ValueLayout.JAVA_BYTE, 0) != 0;
                case DirectWeaverConnection.SHORT:
                    return val.get(ValueLayout.JAVA_SHORT, 0) != 0;
                case DirectWeaverConnection.INT:
                    return val.get(ValueLayout.JAVA_INT, 0) != 0;
                default:
                    throw new ExecutionException("unable to convert");
            }
        }
         @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            val.set(ValueLayout.JAVA_BOOLEAN, 0, (Boolean)value);
            return 1;
        }
    },    
    BYTE {
        @Override
        int getType() {
            return DirectWeaverConnection.BYTE;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case DirectWeaverConnection.BYTE:
                    return val.get(ValueLayout.JAVA_CHAR, 0);
                default:
                    throw new ExecutionException("unable to convert");
            }
        }
        @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            val.set(ValueLayout.JAVA_CHAR, 0, (Character)value);
            return Character.BYTES;
        }
    },
    SHORT {
        @Override
        int getType() {
            return DirectWeaverConnection.SHORT;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case DirectWeaverConnection.SHORT:
                    return val.get(ValueLayout.JAVA_SHORT, 0);
                default:
                    throw new ExecutionException("unable to convert");
            }
        }
        @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            val.set(ValueLayout.JAVA_SHORT, 0, (Short)value);
            return Short.BYTES;
        }
    },
    CHAR {
        @Override
        int getType() {
            return DirectWeaverConnection.CHAR;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            if (varLen < 1) {
                throw new ExecutionException("unable to convert");
            }
            // SQL char is one byte. JAVA_CHAR is two and overruns that slot.
            return (char) (val.get(ValueLayout.JAVA_BYTE, 0) & 0xff);
        }
        @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            val.set(ValueLayout.JAVA_BYTE, 0, (byte) ((Character) value).charValue());
            return 1;
        }
    },
    INTEGER {
        @Override
        int getType() {
            return DirectWeaverConnection.INT;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case DirectWeaverConnection.INT:
                    return val.get(ValueLayout.JAVA_INT, 0);
                default:
                    throw new ExecutionException("unable to convert");
            }
        }
        @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            val.set(ValueLayout.JAVA_INT, 0, (Integer)value);
            return Integer.BYTES;
        }
    },
    FLOAT {
        @Override
        int getType() {
            return DirectWeaverConnection.FLOAT;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case DirectWeaverConnection.FLOAT:
                    return val.get(ValueLayout.JAVA_FLOAT, 0);
                default:
                    throw new ExecutionException("unable to convert");
            }
        }
        @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            val.set(ValueLayout.JAVA_FLOAT, 0, (Float)value);
            return Float.BYTES;
        }
    },
    LONG {
        @Override
        int getType() {
            return DirectWeaverConnection.LONG;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case DirectWeaverConnection.LONG:
                    return val.get(ValueLayout.JAVA_LONG, 0);
                default:
                    throw new ExecutionException("unable to convert");
            }
        }
        @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            val.set(ValueLayout.JAVA_LONG, 0, (Long)value);
            return Long.BYTES;
        }
    },
    DOUBLE {
        @Override
        int getType() {
            return DirectWeaverConnection.DOUBLE;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case DirectWeaverConnection.DOUBLE:
                    return val.get(ValueLayout.JAVA_DOUBLE, 0);
                default:
                    throw new ExecutionException("unable to convert");
            }
        }
        @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            val.set(ValueLayout.JAVA_DOUBLE, 0, (Double)value);
            return Double.BYTES;
        }
    },
    STRING {
        @Override
        int getType() {
            return DirectWeaverConnection.STRING;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case DirectWeaverConnection.STRING:
                case DirectWeaverConnection.TEXT:
                    byte[] chars = new byte[varLen];
                    MemorySegment.copy(val, 0, MemorySegment.ofArray(chars), 0, varLen);
                    return new String(chars, StandardCharsets.UTF_8);
                default:
                    throw new ExecutionException("unable to convert");
            }
        }

        @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            byte[] bytes = ((String) value).getBytes(StandardCharsets.UTF_8);
            if (bytes.length > varLen) {
                throw new ExecutionException("string value exceeds transfer buffer");
            }
            MemorySegment.copy(MemorySegment.ofArray(bytes), 0, val, 0, bytes.length);
            return bytes.length;
        }
    },
    DATE {
        @Override
        int getType() {
            return DirectWeaverConnection.TIMESTAMP;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case DirectWeaverConnection.TIMESTAMP:
                case DirectWeaverConnection.LONG:
                    return new Date(epochMillis(val.get(ValueLayout.JAVA_DOUBLE, 0)));
                default:
                    throw new ExecutionException("unable to convert");
            }
        }

        @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            long millis = value instanceof Instant instant
                    ? instant.toEpochMilli()
                    : ((Date) value).getTime();
            val.set(ValueLayout.JAVA_DOUBLE, 0, secondsSincePostgresEpoch(millis));
            return Double.BYTES;
        }
    },
    INSTANT {
        @Override
        int getType() {
            return DirectWeaverConnection.TIMESTAMP;
        }

        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case DirectWeaverConnection.TIMESTAMP:
                case DirectWeaverConnection.LONG:
                    return Instant.ofEpochMilli(epochMillis(val.get(ValueLayout.JAVA_DOUBLE, 0)));
                default:
                    throw new ExecutionException("unable to convert");
            }
        }

        @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            val.set(ValueLayout.JAVA_DOUBLE, 0, secondsSincePostgresEpoch(((Instant) value).toEpochMilli()));
            return Double.BYTES;
        }
    },
    STREAM {
        @Override
        int getType() {
            return DirectWeaverConnection.STREAM;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case DirectWeaverConnection.STREAM:
                    byte[] buf = new byte[varLen];
                    val.asByteBuffer().get(buf);
                    return buf;
                default:
                    throw new ExecutionException("unable to convert");
            }
        }

        @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            val.setString(0, (String)value);
            return ((String)value).length();
        }
    },
    BINARY {
        @Override
        int getType() {
            return DirectWeaverConnection.BYTEA;
        }

        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case DirectWeaverConnection.BYTEA:
                case DirectWeaverConnection.BLOB:
                case DirectWeaverConnection.STREAM:
                    byte[] buf = new byte[varLen];
                    MemorySegment.copy(val, 0, MemorySegment.ofArray(buf), 0, varLen);
                    return buf;
                default:
                    throw new ExecutionException("unable to convert");
            }
        }

        @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            byte[] bytes = (byte[]) value;
            if (bytes.length > varLen) {
                throw new ExecutionException("bytea value exceeds transfer buffer");
            }
            MemorySegment.copy(MemorySegment.ofArray(bytes), 0, val, 0, bytes.length);
            return bytes.length;
        }
    },
    OBJECT {
        @Override
        int getType() {
            return DirectWeaverConnection.GENERIC;
        }
        
        @Override
        Object read(int type, MemorySegment val, int size) throws ExecutionException {
            switch (type) {
                case DirectWeaverConnection.BOOLEAN:
                    return val.get(ValueLayout.JAVA_BOOLEAN, 0);
                case DirectWeaverConnection.BYTE:
                    return val.get(ValueLayout.JAVA_BYTE, 0);                
                case DirectWeaverConnection.SHORT:
                    return val.get(ValueLayout.JAVA_SHORT, 0); 
                case DirectWeaverConnection.INT:
                    return val.get(ValueLayout.JAVA_INT, 0);    
                case DirectWeaverConnection.FLOAT:
                    return val.get(ValueLayout.JAVA_FLOAT, 0);  
                case DirectWeaverConnection.LONG:
                    return val.get(ValueLayout.JAVA_LONG, 0); 
                case DirectWeaverConnection.DOUBLE:
                    return val.get(ValueLayout.JAVA_DOUBLE, 0); 
                case DirectWeaverConnection.STRING:
                case DirectWeaverConnection.TEXT:
                    return new String(val.toArray(ValueLayout.JAVA_BYTE));
                case DirectWeaverConnection.BYTEA:
                case DirectWeaverConnection.BLOB:
                    byte[] buf = new byte[size];
                    val.asByteBuffer().get(buf);
                    return buf;
                case DirectWeaverConnection.TIMESTAMP:
                    return new Date(epochMillis(val.get(ValueLayout.JAVA_DOUBLE, 0)));
                default:
                    throw new ExecutionException("unable to convert");
            }
        }

        @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            val.setString(0, (String)value);
            return ((String)value).length();
        }
    },
    JAVA {
        @Override
        int getType() {
            return DirectWeaverConnection.JAVA;
        }

        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            byte[] buf = new byte[varLen];
            MemorySegment.copy(val, 0, MemorySegment.ofArray(buf), 0, varLen);
            return WeaverObjectLoader.java_out(buf);
        }

        @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            byte[] bytes = WeaverObjectLoader.java_in(value);
            if (bytes == null) {
                throw new ExecutionException("cannot serialize " + value.getClass().getName());
            }
            if (bytes.length > varLen) {
                throw new ExecutionException("java value exceeds transfer buffer");
            }
            MemorySegment.copy(MemorySegment.ofArray(bytes), 0, val, 0, bytes.length);
            return bytes.length;
        }
    };
    
    /**
     * Seconds between the Unix epoch and the PostgreSQL timestamp epoch
     * (2000-01-01). JNI stores timestamps as this double; both bindings
     * have to agree because the native value is the same 8 bytes.
     */
    static final double POSTGRES_EPOCH_SECONDS = 10957.0 * 86400.0;

    static double secondsSincePostgresEpoch(long epochMillis) {
        return (epochMillis / 1000.0) - POSTGRES_EPOCH_SECONDS;
    }

    static long epochMillis(double secondsSincePostgresEpoch) {
        return (long) ((secondsSincePostgresEpoch + POSTGRES_EPOCH_SECONDS) * 1000.0);
    }

    public static Map<Class<?>, TransferType> types = Map.ofEntries(
        Map.entry(Boolean.class, TransferType.BOOLEAN),
        Map.entry(Byte.class, TransferType.BYTE),
        Map.entry(Character.class, TransferType.CHAR),
        Map.entry(Short.class, TransferType.SHORT),
        Map.entry(Integer.class, TransferType.INTEGER),
        Map.entry(Float.class, TransferType.FLOAT),
        Map.entry(Double.class, TransferType.DOUBLE),
        Map.entry(Long.class, TransferType.LONG),
        Map.entry(String.class, TransferType.STRING),
        Map.entry(Object.class, TransferType.OBJECT),
        Map.entry(Date.class, TransferType.DATE),
        Map.entry(Instant.class, TransferType.INSTANT),
        Map.entry(byte[].class, TransferType.BINARY),
        Map.entry(Serializable.class, TransferType.JAVA)
    );
    
    public static TransferType type(Class<?> type) {
        if (type.isPrimitive()) {
            type = convertPrimative(type);
        }
        if (type != null && type.isArray() && type.getComponentType() == byte.class) {
            return TransferType.BINARY;
        }
        TransferType mapped = types.get(type);
        if (mapped != null) {
            return mapped;
        }
        if (type != null && (ReadableByteChannel.class.isAssignableFrom(type)
                || WritableByteChannel.class.isAssignableFrom(type))) {
            return TransferType.STREAM;
        }
        if (type != null && Serializable.class.isAssignableFrom(type)) {
            return TransferType.JAVA;
        }
        return null;
    }
    
    private static <P> Class convertPrimative(Class type) {
        if (type == Boolean.TYPE) {
            return Boolean.class;
        } else if (type == Byte.TYPE) {
            return Byte.class;
        } else if (type == Character.TYPE) {
            return Character.class;
        } else if (type == Short.TYPE) {
            return Short.class;
        } else if (type == Integer.TYPE) {
            return Integer.class;
        } else if (type == Long.TYPE) {
            return Long.class;
        } else if (type == Float.TYPE) {
            return Float.class;
        } else if (type== Double.TYPE) {
            return Double.class;
        }
        return null;
    }
    
    abstract int getType();
    
    abstract Object read(int type, MemorySegment val, int varLen) throws ExecutionException;
    
    abstract int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException;
}
