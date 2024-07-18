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

import java.util.Properties;
import java.util.concurrent.TimeUnit;

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
            vars.append(key + "=" + props.getProperty(key) + ";");
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
    
    public static void close(boolean clean) {
        boolean wasInterruped = false;
        if (clean) {
            try {
                while (DBReference.hasLiveConnections()) {
                    TimeUnit.SECONDS.sleep(10);
                }
            } catch (InterruptedException ie) {
                wasInterruped = true;
            }
        }
        close();
        if (wasInterruped) {
            Thread.currentThread().interrupt();
        }
    }
}
