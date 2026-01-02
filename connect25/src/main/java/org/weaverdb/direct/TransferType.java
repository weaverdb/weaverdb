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
import java.util.Map;
import org.weaverdb.ExecutionException;
import static org.weaverdb.direct.DirectWeaverConnection.bindCharacter;
import static org.weaverdb.direct.DirectWeaverConnection.bindDouble;
import static org.weaverdb.direct.DirectWeaverConnection.bindFloat;
import static org.weaverdb.direct.DirectWeaverConnection.bindInteger;
import static org.weaverdb.direct.DirectWeaverConnection.bindLong;
import static org.weaverdb.direct.DirectWeaverConnection.bindShort;
import static org.weaverdb.direct.DirectWeaverConnection.bindString;

/**
 *
 */
enum TransferType {
    BOOLEAN {
        @Override
        int getType() {
            return DirectWeaverConnection.bindBoolean;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case bindCharacter:
                    return val.get(ValueLayout.JAVA_CHAR, 0) != 0;
                case bindShort:
                    return val.get(ValueLayout.JAVA_SHORT, 0) != 0;
                case bindInteger:
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
    CHAR {
        @Override
        int getType() {
            return DirectWeaverConnection.bindCharacter;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case bindCharacter:
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
            return DirectWeaverConnection.bindShort;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case bindShort:
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
    INTEGER {
        @Override
        int getType() {
            return DirectWeaverConnection.bindInteger;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case bindInteger:
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
            return DirectWeaverConnection.bindFloat;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case bindFloat:
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
            return DirectWeaverConnection.bindLong;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case bindLong:
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
            return DirectWeaverConnection.bindDouble;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case bindDouble:
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
            return DirectWeaverConnection.bindString;
        }
        
        @Override
        Object read(int type, MemorySegment val, int varLen) throws ExecutionException {
            switch (type) {
                case bindString:
                    return val.getString(0);
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
    
    public static Map<Class<?>, TransferType> types = Map.of(
        Boolean.class, TransferType.BOOLEAN,
        Character.class, TransferType.CHAR,
        Short.class, TransferType.SHORT,
        Integer.class, TransferType.INTEGER,
        String.class, TransferType.STRING
    );
    
    abstract int getType();
    
    abstract Object read(int type, MemorySegment val, int varLen) throws ExecutionException;
    
    abstract int write(Object value, int type, MemorySegment val, int varLen) throws ExecutionException;
}
