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
import java.lang.foreign.ValueLayout;
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
    private MemorySegment upcall;
    private String column;
    private T value;

    DirectOutput(int index, Class<T> type) {
        this.index = index;
        this.type = TransferType.type(type);
        this.upcall = Linker.nativeLinker().upcallStub(transferOut.bindTo(this), FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT, ADDRESS, JAVA_INT), Arena.ofAuto());
    }
    
    int getType() {
        return type.getType();
    }
    
    private int transferOut(MemorySegment user, int type, MemorySegment var, int varSize) {
        try (Arena a = Arena.ofConfined()) {
            if (varSize < 0) {
                value = null;
                return 0;
            } else {
                var = var.reinterpret(varSize, a, null);
                if (type == DirectWeaverConnection.META) {
                    column = new String(var.toArray(ValueLayout.JAVA_BYTE));
                } else {
                    value = (T)this.type.read(type, var, varSize);
                }
            }
        } catch (ExecutionException ee) {
            value = null;
        }
        return varSize;
    }
    
    T get() {
        return value;
    }
    
    protected void set(T value) {
        this.value = value;
    }
    
    MemorySegment createUpcallStub() {
        MethodHandle target = getTransferFunction();
        target = target.bindTo(this);
        this.upcall = Linker.nativeLinker().upcallStub(target, FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT, ADDRESS, JAVA_INT), Arena.ofAuto());
        return this.upcall;
    }
    
    MethodHandle getTransferFunction() {
        return transferOut;
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
