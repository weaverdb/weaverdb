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

/**
 * This is the main interface into the database.  Most interaction 
 * is done via @see org.weaverdb.Statement.  Instances of DBReference 
 * hold references to native resources and should always be closed on 
 * when they are no longer in use.
 */
public interface DBReference extends AutoCloseable {
    
    static DBReferenceFactory loader = loadConnectionFactory();
    /**
     * Begin a transaction.  
     * @return transaction id.
     * @throws ExecutionException if anything goes wrong or the current reference
     * is not in the correct state (transaction already in progress)
     */
    long begin() throws ExecutionException;
    /**
     * Abort a transaction. Does nothing if no transaction is in progress
     */
    void abort();
    /**
     * Commit a transaction.
     * 
     * @throws ExecutionException if this reference is not in the correct state 
     * for commit (no open transaction)
     */
    void commit() throws ExecutionException;
    /**
     * Start a procedure.  Procedures are a group of statements that should be 
     * considered a single execution unit.  This mainly has affects visibly of 
     * statements earlier in the procedure.  Results of previously executed statements
     * will not be visible subsequent statement executions until the procedure is 
     * ended.
     * 
     * @throws ExecutionException 
     */
    void start() throws ExecutionException;
    /**
     * End a procedure. @see org.weaverdb.DBReference#start
     * @throws ExecutionException 
     */
    void end() throws ExecutionException;
    /**
     * Cancel the currently executing statement.  Attempt ceasing execution
     * of the currently executing statement.  Useful when helper threads are 
     * involved in execution
     * @throws ExecutionException 
     */
    void cancel() throws ExecutionException;
    /**
     * Execute a statement in the database.  Parses and executes the statement in
     * the current reference.
     * @param statement SQL statement
     * @return number of elements in the database that have been modified or added 
     *      as a result of this execution
     * @throws ExecutionException 
     */
    long execute(String statement) throws ExecutionException;
    /**
     * Is this reference still valid.
     * @return true if valid
     */
    boolean isValid();
    /**
     * Set the input stream of this reference for @see org.weaverdb.DBReference#stream
     * calls
     * @param in 
     */
    void setStandardInput(InputStream in);
    /**
     * Set the output stream of this reference for @see org.weaverdb.DBReference#stream
     * calls
     * @param out 
     */
    void setStandardOutput(OutputStream out);
    /**
     * Parse a statement on the current reference.
     * @param stmt SQL statement to be parsed
     * @return A statement object to link input/output, execute and fetch
     * @throws ExecutionException 
     */
    Statement statement(String stmt) throws ExecutionException;
    /**
     * Execute a statement using solely the standard input and output of the reference.
     * This is useful for debugging statements like EXPLAIN that do not fit prepared statements.
     * 
     * @param stmt SQL statement to parse and execute
     * @throws ExecutionException 
     */
    void stream(String stmt) throws ExecutionException;
    /**
     * Current transaction id.
     * @return current transaction id or -1L for invalid transaction
     */
    long transaction();
    /**
     * Close this reference and release any native held resources.
     * @throws ExecutionException 
     */
    @Override
    public void close() throws ExecutionException;
    /**
     * Connect and retrieve a reference to a database in the current Weaver instance.  
     * Weaver must be loaded and initialized before this call will work.  
     * @see org.weaverdb.WeaverInitializer#initialize
     * @param database name of the database to connect to
     * @return 
     */
    static DBReference connect(String database) {
        return loader.connect(database);
    }
    
    private static DBReferenceFactory loadConnectionFactory() {
        Runtime.Version runningVersion = Runtime.version();
        ServiceLoader<DBReferenceFactory> check = ServiceLoader.load(DBReferenceFactory.class);
        Iterator<DBReferenceFactory> versions = check.iterator();
        DBReferenceFactory winner = null;
        while (versions.hasNext()) {
            DBReferenceFactory candidate = versions.next();
            if (candidate.builtFor().compareTo(runningVersion) <= 0) {
                if (winner == null || winner.builtFor().compareTo(candidate.builtFor()) < 0) {
                    winner = candidate;
                }
            }
        }
        return winner;
    }
}
