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
import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import org.weaverdb.ExecutionException;

/**
 *
 */
class DirectInput<T> {    
    private static final MethodHandle transferIn;
    
    static {
        MethodHandles.Lookup lookup = MethodHandles.lookup();
        MethodHandle temp = null;
        try {
            temp = lookup.findVirtual(DirectInput.class, "transferIn", MethodType.methodType(int.class, MemorySegment.class, int.class, MemorySegment.class, int.class));
        } catch (IllegalAccessException | NoSuchMethodException ia) {

        }
        transferIn = temp;
    }
    
    private final String name;
    private final TransferType type;
    private MemorySegment upcall;
    private String column;
    private T value;

    DirectInput(String name, Class<T> type) {
        this.name = name;
        this.type = TransferType.type(type);
    }
    
    int getType() {
        return type.getType();
    }
    
    private int transferIn(MemorySegment user, int type, MemorySegment var, int varSize) {
        try {
            // WeaverConnection probes BYTEA/TEXT/etc with NULL + LENGTH_QUERY_OP
            // before allocating the transfer buffer (see PassInValue / ExtractBytes).
            if (varSize == DirectWeaverConnection.LENGTH_QUERY_OP
                    || var.equals(MemorySegment.NULL)) {
                if (value == null) {
                    return -1;
                }
                if (value instanceof byte[] bytes) {
                    return bytes.length;
                }
                if (value instanceof String s) {
                    return s.length();
                }
                return 1;
            }
            if (varSize == DirectWeaverConnection.NULL_CHECK_OP) {
                return value != null ? 1 : -1;
            }
            try (Arena a = Arena.ofConfined()) {
                // Native pointers arrive as zero-length segments; expand like DirectOutput.
                var = var.reinterpret(varSize, a, null);
                return this.type.write(value, type, var, varSize);
            }
        } catch (ExecutionException ee) {

        }
        return 0;
    }
    
    MemorySegment createUpcallStub() {
        MethodHandle target = getTransferFunction();
        target = target.bindTo(this);
        this.upcall = Linker.nativeLinker().upcallStub(target, FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT, ADDRESS, JAVA_INT), Arena.ofAuto());
        return this.upcall;
    }
    
    MethodHandle getTransferFunction() {
        return transferIn;
    }
    
    void set(T value) {
        this.value = value;
    }
    
    T get() {
        return value;
    }
    
    String getName() {
        return column;
    }
    
    void reset() {
        value = null;
    }
    
    void deactivate() {
       
    }
}
