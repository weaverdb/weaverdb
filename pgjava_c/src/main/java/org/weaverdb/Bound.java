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

package org.weaverdb;

/**
 * Base type for Bound Input/Output.
 * @author  mscott
 */
class Bound<T> {

    private final Types settype;
    private final Class<T> type; 
    private boolean orphaned;

    Bound(Class<T> type) {
        this.type = type.isPrimitive() ? convertPrimative(type) : type;
        this.settype = bind(this.type);
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
    
    private static Types bind(Class<?> type) {
        if (type.equals(String.class)) {
            return Types.String;
        } else if (type.equals(Double.class)) {
            return Types.Double;
        } else if (type.equals(Float.class)) {
            return Types.Float;
        } else if (type.equals(Integer.class)) {
            return Types.Integer;
        } else if (type.equals(byte[].class)) {
            return Types.Binary;
        } else if (type.equals(java.io.ByteArrayInputStream.class)) {
            return Types.BLOB;
        } else if (type.equals(java.io.ByteArrayOutputStream.class)) {
            return Types.BLOB;
        } else if (type.equals(Character.class)) {
            return Types.Character;
        } else if (type.equals(java.util.Date.class)) {
            return Types.Date;
        } else if (type.equals(java.time.Instant.class)) {
            return Types.Date;
        } else if (type.equals(Long.class)) {
            return Types.Long;
        } else if (type.equals(Boolean.class)) {
            return Types.Boolean;
        } else if (java.nio.channels.WritableByteChannel.class.isAssignableFrom(type)) {
            return Types.Direct;
        } else if (java.nio.channels.ReadableByteChannel.class.isAssignableFrom(type)) {
            return Types.Direct;
        } else if (java.io.OutputStream.class.isAssignableFrom(type)) {
            return Types.Stream;
        } else if (java.io.InputStream.class.isAssignableFrom(type)) {
            return Types.Stream;
        } else if (java.io.Serializable.class.isAssignableFrom(type)) {
            return Types.Java;
        } else {
            return Types.Null;
        }
    }
    
    protected enum Types {
        String(BaseWeaverConnection.bindString,"Ljava/lang/String;"),
        Double(BaseWeaverConnection.bindDouble,"D"),
        Float(BaseWeaverConnection.bindFloat,"F"),
        Integer(BaseWeaverConnection.bindInteger,"I"),
        Binary(BaseWeaverConnection.bindBinary,"[B"),
        BLOB(BaseWeaverConnection.bindBLOB,"[B"),
        Character(BaseWeaverConnection.bindCharacter,"C"),
        Boolean(BaseWeaverConnection.bindBoolean,"Z"),
        Date(BaseWeaverConnection.bindDate,"[B"),
        Long(BaseWeaverConnection.bindLong,"[B"),
        Function(BaseWeaverConnection.bindFunction,"[B"),
        Slot(BaseWeaverConnection.bindSlot,"[B"),
        Java(BaseWeaverConnection.bindJava,"[B"),
        Text(BaseWeaverConnection.bindText,"[B"),
        Stream(BaseWeaverConnection.bindStream,"<assigned>"),
        Direct(BaseWeaverConnection.bindDirect,"<direct>"),
        Null(BaseWeaverConnection.bindNull,"");
                
        private final int id;
        private final String signature;
        
        Types(int id, String sig) {
            this.id = id;
            this.signature = sig;
        }
        
        int getId() {
            return id;
        }
        
        String getSignature() {
            return signature;
        }
    }

    Class<T> getTypeClass() {
        return type;
    }

    boolean isSameType(Class t) {
        return t.equals(type);
    }

    Types getType() {
        return settype;
    }

    int getTypeId() {
        return settype.getId();
    }

    void deactivate() {
        orphaned = false;
    }
    
    boolean isOrphaned() {
        return orphaned;
    }
}
