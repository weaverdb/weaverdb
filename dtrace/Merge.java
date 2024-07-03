/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */



import java.io.*;

public class Merge {
    
    
    public static void main(String[] args) {
        try {
            FileReader[] f1 = new FileReader[args.length];
            for (int x=0;x<args.length;x++) {
                f1[x] = new FileReader(args[x]);
            }
            
            LineNumberReader[] l1 = new LineNumberReader[args.length];
            for (int x=0;x<args.length;x++) {
                l1[x] = new LineNumberReader(f1[x]);
            }
            
            boolean cont = false;
            String[] s1 = new String[args.length];
            for (int x=0;x<args.length;x++) {
                s1[x] = l1[x].readLine();
                if ( s1[x] != null ) cont = true;
            }
            
            while ( cont ) {
                StringBuilder add = new StringBuilder();
                for (int x=0;x<args.length;x++) {
                    if ( s1[x] != null ) {
                        add.append(s1[x].trim());
                    }
                    add.append(',');                    
                }
                
                if ( add.length() > args.length ) {
                    add.setLength(add.length() - 1);
                    System.out.println(add.toString());
                }
                
                cont = false;
                for (int x=0;x<args.length;x++) {
                    s1[x] = l1[x].readLine();
                    if ( s1[x] != null ) cont = true;
                }
            }
        } catch ( IOException ioe ) {
            ioe.printStackTrace();
        }
    }
    
}

