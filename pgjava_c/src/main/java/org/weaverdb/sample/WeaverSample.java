/*-------------------------------------------------------------------------
 *
 *
 * Copyright (c) 2024, Myron Scott  <myron@weaverdb.org>
 *
 * All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 *
 *-------------------------------------------------------------------------
 */
package org.weaverdb.sample;

import java.io.Writer;
import java.util.Properties;
import java.util.stream.Stream;
import org.weaverdb.Input;
import org.weaverdb.Output;
import org.weaverdb.FetchSet;
import org.weaverdb.FetchSet.Column;
import org.weaverdb.FetchSet.Row;
import org.weaverdb.Statement;
import org.weaverdb.WeaverInitializer;
import org.weaverdb.DBReference;

/**
 *
 */
public class WeaverSample {

    public static void main(String[] args) throws Exception {
        // create the database home using initdb
        initdb();
        // use the postgres executable to create the sample database
        createSampleDatabase();
        // sample database is ready, load native library to start db
        startWeaver();
        // create a new table
        createSampleTable();
        // select using prepared statements
        selectFromSampleTableUsingPreparedStatements();
        // select using resultset builder
        selectFromSampleTableUsingResultSet();
    }
    
    private static void initdb() throws Exception {
        // create the database home using initdb
        ProcessBuilder b = new ProcessBuilder("mtpg/bin/initdb","-D", System.getProperty("user.dir") + "/testdb");
        b.inheritIO();
        Process p = b.start();
        p.waitFor();
    }
    
    private static void createSampleDatabase() throws Exception {
        ProcessBuilder b = new ProcessBuilder("mtpg/bin/postgres", "-D", System.getProperty("user.dir") + "/testdb", "-o", "/dev/null", "template1");
        b.redirectOutput(ProcessBuilder.Redirect.INHERIT);
        b.redirectError(ProcessBuilder.Redirect.INHERIT);
        Process p = b.start();
        try (Writer w = p.outputWriter()) {
            w.append("create database sample;\n").flush();
        }
        p.waitFor();
    }
    
    private static void startWeaver() {
        Properties prop = new Properties();
        prop.setProperty("datadir", System.getProperty("user.dir") + "/testdb"); // tell the native library where database is (required)
        prop.setProperty("allow_anonymous", "true"); // allow connection to the database without username/password
        prop.setProperty("stdlog", "TRUE"); // print debug logging to stdout
        prop.setProperty("disable_crc", "TRUE"); // disable crc checking on buffer pages for now
        WeaverInitializer.initialize(prop); // start connect to the database
        Runtime.getRuntime().addShutdownHook(new Thread(()->WeaverInitializer.close(true))); // call close when the JVM is shutdown
    }
    
    private static void createSampleTable() throws Exception {
        try (DBReference c = DBReference.connect("sample")) {
            c.execute("create table example (id int4, name varchar)"); // use execute if there is output expected from the statement
            // insert some values into the table
            try (Statement s = c.statement("insert into example (id, name) values ($sid, $sname)")) { 
                // link the input variables
                Input<Integer> id = s.linkInput("sid", Integer.class); // link the input variables
                Input<String> name = s.linkInput("sname", String.class);
                // insert some values
                id.set(1);
                name.set("Tom");
                s.execute(); // insert id = 1, name = "Tom" into table sample
                
                id.set(2);
                name.set("Mary");
                s.execute(); // insert id = 2, name = "Mary" into table sample
            } // try with resources to close statement after use
        } // try with resources to close connection after use
    }

    private static void selectFromSampleTableUsingPreparedStatements() throws Exception {
        try (DBReference c = DBReference.connect("sample")) {
            try (Statement s = c.statement("select id, name from example where id = $id")) { 
                // link the output/input variables
                Output<Integer> id = s.linkOutput(1, Integer.class); 
                Output<String> name = s.linkOutput(2, String.class);
                Input<Integer> selector = s.linkInput("id", Integer.class);
                // select the row
                selector.set(2);
                s.execute(); // find the correct row
                if (s.fetch()) { // fetch the row true means data available
                    System.out.println("id=" + id.get()); // id=2
                    System.out.println("name=" + name.get()); // name=Mary
                }
            } // try with resources to close statement after use
        } // try with resources to close connection after use
    }
    
    private static void selectFromSampleTableUsingResultSet() throws Exception {
        try (DBReference c = DBReference.connect("sample")) {
            FetchSet.Builder builder = org.weaverdb.FetchSet.builder(c);
            try (Stream<Row> rs = builder.parse("select id, name from example where id = $id")
                    .input("id", 1)
                    .output(1, Integer.class)
                    .output(2, String.class)
                    .execute()) {
                rs.flatMap(Row::stream).forEach((Column column)->{
                    System.out.println(column.getName() + "=" + column.get()); // id=1 name=Tom
                });
            } // try with resources to close stream after use
        } // try with resources to close connection after use // try with resources to close connection after use
    } 
}
