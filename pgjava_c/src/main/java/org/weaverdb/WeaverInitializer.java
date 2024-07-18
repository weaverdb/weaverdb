/*-------------------------------------------------------------------------
 *
 *	WeaverInitializer.java
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

import java.time.Duration;
import java.time.Instant;
import java.util.Properties;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

public class WeaverInitializer {
    
    private static boolean loaded = false;
    
    public WeaverInitializer() {
        
    }
    
    private static synchronized native void init(String database);
    private static synchronized native void close();
    
    public static void initialize(Properties props) throws java.lang.UnsatisfiedLinkError  {
        StringBuilder vars = new StringBuilder();
        
        if ( loaded ) return;
        
        java.util.Enumeration it = props.keys();
        while ( it.hasMoreElements() ) {
            String key = it.nextElement().toString();
            vars.append(key).append("=").append(props.getProperty(key)).append(";");
        }
        
        String library = props.getProperty("library");
        if ( library == null ) library = "weaver";
        try {
            init(vars.toString());
        } catch ( UnsatisfiedLinkError us ) {
            System.loadLibrary(library);
            
            init(vars.toString());
        }
        
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
