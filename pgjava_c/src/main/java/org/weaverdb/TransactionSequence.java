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


public class TransactionSequence implements AutoCloseable {
    private final DBReference connection;
    private final long current;

    public TransactionSequence(DBReference connection) throws ExecutionException {
        this.connection = connection;
        this.current = connection.begin();
        if (this.current <= 0) {
            throw new ExecutionException("transaction not started");
        }
    }
    
    public Statement statement(String stmt) throws ExecutionException {
        return connection.statement(stmt);
    }
    
    public long execute(String stmt)  throws ExecutionException {
        return connection.execute(stmt);
    }
        
    public Procedure start() throws ExecutionException {
        return new Procedure();
    }

    @Override
    public void close() throws ExecutionException {
        if (connection.transaction() == current) {
            try {
                connection.commit();
            } catch (ExecutionException ee) {
                connection.abort();
            }
        }
    }
    
    public class Procedure implements AutoCloseable {

        Procedure() throws ExecutionException {
            connection.start();
        }

        public Statement statement(String stmt) throws ExecutionException {
            return connection.statement(stmt);
        }
        
        public long execute(String stmt)  throws ExecutionException {
            return connection.execute(stmt);
        }
    
        @Override
        public void close() throws ExecutionException {
            connection.end();
        }
        
    }
    
    
}
