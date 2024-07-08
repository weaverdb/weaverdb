/*-------------------------------------------------------------------------
 *
 *	WeaverObjectLoader.java
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

import java.io.*;
import java.lang.reflect.*;

/**
 *
 * @author  mscott
 */
public class WeaverObjectLoader extends ThreadLocal<WeaverObjectLoader> implements ObjectStreamConstants {
    
    static WeaverObjectLoader loc = new WeaverObjectLoader();
    
    
    ByteWellInputStream in;
    ObjectInputStream oin;
    ByteWellOutputStream out;
    ObjectOutputStream oout;
    
    public WeaverObjectLoader() {
        try {
            out = new ByteWellOutputStream();
            oout = new ObjectOutputStream(out);
            oout.flush();
            /* prime the input stream with the data from the input stream  */
            in = new ByteWellInputStream();
            byte[] well = out.getByteWell();
            in.setByteWell(well);
            oin = new ObjectInputStream(in);
        } catch ( IOException ioe ) {
            ioe.printStackTrace();
        }
    }
    
    public WeaverObjectLoader initialValue() {
        return new WeaverObjectLoader();
    }
    
    public Object out(byte[] input) {
        try {
            in.setByteWell(input);
            return oin.readObject();
        } catch ( IOException ioe ) {
            ioe.printStackTrace();
            return null;
        } catch ( ClassNotFoundException ce ) {
            ce.printStackTrace();
            return null;
        } catch ( Throwable t ) {
            t.printStackTrace();
            return null;
        }
    }
    
    public static Object java_out(byte[] input) {
        return loc.get().out(input);
    }
    
    public static boolean java_equals(byte[] obj1,byte[] obj2) {
        WeaverObjectLoader jc = loc.get();
        
        Object o1 = jc.out(obj1);
        Object o2 = jc.out(obj2);
        
        return o1.equals(o2);
    }
    
    public static int java_compare(byte[] obj1,byte[] obj2) {
        WeaverObjectLoader jc = loc.get();
        
        Comparable o1 = (Comparable)jc.out(obj1);
        Comparable o2 = (Comparable)jc.out(obj2);
        
        return o1.compareTo(o2);
    }
    
    public byte[] in(Object input) {
        try {
            oout.reset();
            oout.writeObject(input);
            oout.flush();
            byte[] b = out.getByteWell();
            return b;
        } catch ( IOException ioe ) {
            return null;
        }
    }
    
    public static byte[] java_in(Object input) {
        return loc.get().in(input);
    }
    
    public static byte[] java_text_in(String input) {
        if (input.charAt(0) == 'L' && input.charAt(input.length() -1) == ';') {
            int br = input.indexOf('(');
            String name = input.substring(0, br ).trim();
            String arg = input.substring(br).trim();

            arg = arg.substring(1,arg.length()-2);
            try {
                Class c = Class.forName(name);

                Constructor cont = c.getConstructor(String.class);
                Object obj = cont.newInstance(arg);
                return java_in(obj);
            } catch ( Exception exp ) {
                exp.printStackTrace();
                return null;
            }
        } else {
            return java_in(input);
        }
    }
    
    public static String java_text_out(byte[] output) {
        Object obj = java_out(output);
        Class name = obj.getClass();
        StringBuilder build = new StringBuilder();
        
        build.append(name.getName());
        build.append('(');
        build.append(obj.toString());
        build.append(')');
        
        return build.toString();
    }
}
