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

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.time.Duration;
import java.time.Instant;
import java.util.Properties;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

public class DirectWeaverInitializer {
    
    private static boolean loaded = false;
    
    private static final MethodHandle initWeaverBackend;
    private static final MethodHandle wrapupWeaverBackend;

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
    }
    
    public DirectWeaverInitializer() {
        
    }
    
    private static synchronized void init(String database) {
        try (Arena arena = Arena.ofConfined()) {
            boolean success = (boolean) initWeaverBackend.invokeExact(arena.allocateFrom(database));
            if (!success) {
                throw new UnsatisfiedLinkError("environment not valid, see db log");
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
            while (DBReference.hasLiveConnections() && start.plus(timeout).isAfter(Instant.now())) {
                TimeUnit.SECONDS.sleep(1);
            }
            if (DBReference.hasLiveConnections()) {
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
}
