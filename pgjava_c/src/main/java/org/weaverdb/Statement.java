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

import java.nio.channels.WritableByteChannel;
import java.util.Collection;
import java.util.function.Supplier;
/**
 * Prepared statement derived from @see org.weaverdb.DBReference.  Must be closed 
 * after use either directly or with try-with-resources in order to release native
 * held resources.
 */
public interface Statement extends AutoCloseable {
    /**
     * Current command id
     * @return command id
     */
    long command();
    /**
     * Execute the current prepared statement. 
     * @return number of modified rows
     * @throws ExecutionException 
     */
    long execute() throws ExecutionException;
    /**
     * Fetch the next row in this select statement.
     * @return true if a row was fetched, false means no more rows available
     * @throws ExecutionException 
     */
    boolean fetch() throws ExecutionException;
    /**
     * Is the current statement valid.
     * @return true if valid
     */
    boolean isValid();
    /**
     * Link a named input variable in the parsed statement.
     * @param <T> requested type.  Valid types are:
     *      Boolean,Char,Short,Integer,Long,Float,Double,String,byte[],Date,Instant
     * @param name name of the variable
     * @param type Class of requested type
     * @return bound Input @see org.weaverdb.Input
     * @throws ExecutionException 
     */
    <T> Input<T> linkInput(String name, Class<T> type) throws ExecutionException;
    /**
     * Link a named input variable in the parsed statement to a Channel which is transformed 
     * using the supplied function.  This links a blob or text field in the input to an arbitrary 
     * java type that is written to a WritableChannel via the transform function.  This linkage spawns 
     * a helper thread in the background to pipe data from the database to the transformer.
     * @param <T> any arbitrary java type
     * @param name name of the variable
     * @param transform function to write the java object to a WritableChannel.
     * @return bound Input @see org.weaverdb.Input
     * @throws ExecutionException 
     */
    <T> Input<T> linkInputChannel(String name, Input.Channel<T> transform) throws ExecutionException;
    /**
     * Link a named input variable in the parsed statement to a Stream which is transformed 
     * using the supplied function.  This is a convenience linkage for when a Stream is preferred 
     * over a Channel.  The underlying implementation calls the Channel version with a translation to 
     * stream. @see org.weaverdb.Statement#linkInputChannel
     * @param <T> any arbitrary java type
     * @param name name of the variable
     * @param transform function to write the java object to a OutputStream.
     * @return bound Input @see org.weaverdb.Input
     * @throws ExecutionException 
     */
    <T> Input<T> linkInputStream(String name, Input.Stream<T> transform) throws ExecutionException;
    /**
     * Link an indexed output variable in the parsed statement.
     * @param <T> requested type.  Valid types are:
     *      Boolean,Char,Short,Integer,Long,Float,Double,String,byte[],Date,Instant
     * @param index the index of the output column of the parsed select statement
     * @param type Class of requested type
     * @return bound Output @see org.weaverdb.Output
     * @throws ExecutionException 
     */
    <T> Output<T> linkOutput(int index, Class<T> type) throws ExecutionException;
    /**
     * Link an index output variable in the parsed statement to a Channel which is transformed 
     * using the supplied function.  This links a blob or text field in the input to an arbitrary 
     * java type that is read from a ReadableChannel via the transform function.  This linkage spawns a helper
     * thread in the background to pipe data from the database to the transformer
     * @param <T> any arbitrary java type
     * @param index index of the column in the parsed select statement
     * @param transform function to write the java object to a ReadableChannel.
     * @return bound Output @see org.weaverdb.Output
     * @throws ExecutionException 
     */
    <T> Output<T> linkOutputChannel(int index, Output.Channel<T> transform) throws ExecutionException;
    /**
     * Link an index output variable in the parsed statement to supplied channel which will be written to
     * by the database on fetch.  Supply the statement with a channel to be written to when the fetch occurs.
     * @param <T> WritableByteChannel or derivative
     * @param index index of the column in the parsed select statement
     * @param cstor provides the WriteableByteChannel that the statement will directly write the variable to.
     * @return bound Output @see org.weaverdb.Output with the created WriteableByteChannel returned with the get() call.
     * @throws ExecutionException 
     */
    <T extends WritableByteChannel> Output<T> linkOutputChannel(int index, Supplier<T> cstor) throws ExecutionException;
    /**
     * Link an index output variable in the parsed statement to a Channel which is transformed 
     * using the supplied function.  This is a convenience linkage for when a Stream is preferred 
     * over a Channel.  The underlying implementation calls the Channel version with a translation to 
     * stream. @see org.weaverdb.Statement#linkOutputChannel
     * @param <T> any arbitrary java type
     * @param index index of the column in the parsed select statement
     * @param transform function to write the java object to a ReadableChannel.
     * @return bound Output @see org.weaverdb.Output
     * @throws ExecutionException 
     */
    <T> Output<T> linkOutputStream(int index, Output.Stream<T> transform) throws ExecutionException;
    /**
     * Currently linked outputs on this statement.
     * @return collection of currently linked outputs
     */
    Collection<Output> outputs();
    
    Collection<Input> inputs();
    
    @Override
    void close();
    
}
