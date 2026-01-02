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
class DirectOutput<T> {
    private final static MethodHandle transferOut;
    
    static {
        MethodHandles.Lookup lookup = MethodHandles.lookup();
        MethodHandle temp = null;
        try {
            temp = lookup.findVirtual(DirectOutput.class, "transferOut", MethodType.methodType(int.class, MemorySegment.class, int.class, MemorySegment.class, int.class));
        } catch (IllegalAccessException | NoSuchMethodException ia) {

        }
        transferOut = temp;
    }
    
    private final int index;
    private final TransferType type;
    private final MemorySegment upcall;
    private String column;
    private T value;

    DirectOutput(int index, Class<T> type) {
        this.index = index;
        this.type = TransferType.types.get(type);
        this.upcall = Linker.nativeLinker().upcallStub(transferOut.bindTo(this), FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT, ADDRESS, JAVA_INT), Arena.ofAuto());
    }
    
    int getType() {
        return type.getType();
    }
    
    int transferOut(MemorySegment user, int type, MemorySegment var, int varSize) {
        try {
            value = (T)this.type.read(type, var, varSize);
        } catch (ExecutionException ee) {
            value = null;
        }
        return varSize;
    }
    
    T get() {
        return value;
    }
    
    protected void setValue(T value) {
        this.value = value;
    }
    
    MemorySegment getUpcallStub() {
        return upcall;
    }
    
    String getName() {
        return column;
    }
    
    int getIndex() {
        return index;
    }
    
    void reset() throws ExecutionException {
        value = null;
    }
    
    void deactivate() {
       
    }
}
