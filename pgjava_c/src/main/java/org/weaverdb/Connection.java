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

import java.io.InputStream;
import java.io.OutputStream;
import java.util.Iterator;
import java.util.ServiceLoader;

public interface Connection extends AutoCloseable {
    
    static ConnectionFactory loader = loadConnectionFactory();

    void abort();

    long begin() throws ExecutionException;

    void cancel() throws ExecutionException;

    void commit() throws ExecutionException;

    void end() throws ExecutionException;

    long execute(String statement) throws ExecutionException;

    boolean isValid();

    void prepare() throws ExecutionException;

    void setStandardInput(InputStream in);

    void setStandardOutput(OutputStream out);

    void start() throws ExecutionException;

    Statement statement(String stmt) throws ExecutionException;

    void stream(String stmt) throws ExecutionException;

    long transaction();
    
    @Override
    public void close() throws ExecutionException;
        
    Connection helper() throws ExecutionException;
    
    static Connection connect(String database) {
        return loader.connect(database);
    }
    
    static Connection connectUser(String username, String password, String database) {
        return loader.connectUser(username, password, database);
    }
    
    private static ConnectionFactory loadConnectionFactory() {
        Runtime.Version runningVersion = Runtime.version();
        ServiceLoader<ConnectionFactory> check = ServiceLoader.load(ConnectionFactory.class);
        Iterator<ConnectionFactory> versions = check.iterator();
        ConnectionFactory winner = null;
        while (versions.hasNext()) {
            ConnectionFactory candidate = versions.next();
            if (candidate.builtFor().compareTo(runningVersion) <= 0) {
                if (winner == null || winner.builtFor().compareTo(candidate.builtFor()) < 0) {
                    winner = candidate;
                }
            }
        }
        return winner;
    }
}
