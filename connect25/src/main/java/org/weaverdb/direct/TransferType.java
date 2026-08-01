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

import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
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
            switch (type) {
                case DirectWeaverConnection.CHAR:
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
                    return new String(val.toArray(ValueLayout.JAVA_BYTE));
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
                    long date = val.get(ValueLayout.JAVA_LONG, 0);
                    return new Date(date);
                default:
                    throw new ExecutionException("unable to convert");
            }
        }

        @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            val.set(ValueLayout.JAVA_LONG, 0, ((Date)value).getTime());
            return Long.BYTES;
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
                    long date = val.get(ValueLayout.JAVA_LONG, 0);
                    return new Date(date);
                default:
                    throw new ExecutionException("unable to convert");
            }
        }

        @Override
        int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException {
            val.setString(0, (String)value);
            return ((String)value).length();
        }
    };
    
    public static Map<Class<?>, TransferType> types = Map.ofEntries(
        Map.entry(Boolean.class, TransferType.BOOLEAN),
        Map.entry(Byte.class, TransferType.BYTE),
        Map.entry(Character.class, TransferType.CHAR),
        Map.entry(Short.class, TransferType.SHORT),
        Map.entry(Integer.class, TransferType.INTEGER),
        Map.entry(Float.class, TransferType.FLOAT),
        Map.entry(Long.class, TransferType.LONG),
        Map.entry(String.class, TransferType.STRING),
        Map.entry(Object.class, TransferType.OBJECT),
        Map.entry(Date.class, TransferType.DATE),
        Map.entry(byte[].class, TransferType.BINARY)
    );
    
    public static TransferType type(Class<?> type) {
        if (type.isPrimitive()) {
            type = convertPrimative(type);
        }
        if (type != null && type.isArray() && type.getComponentType() == byte.class) {
            return TransferType.BINARY;
        }
        return types.get(type);
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
