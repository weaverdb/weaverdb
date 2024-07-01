
package org.weaverdb;

import java.nio.channels.WritableByteChannel;
import java.util.Collection;
import java.util.function.Supplier;


public interface Statement extends AutoCloseable {

    long command();

    long execute() throws ExecutionException;

    boolean fetch() throws ExecutionException;

    boolean isValid();

    <T> Input<T> linkInput(String name, Class<T> type) throws ExecutionException;

    <T> Input<T> linkInputChannel(String name, Input.Channel<T> transform) throws ExecutionException;

    <T> Input<T> linkInputStream(String name, Input.Stream<T> transform) throws ExecutionException;

    <T> Output<T> linkOutput(int index, Class<T> type) throws ExecutionException;

    <T> Output<T> linkOutputChannel(int index, Output.Channel<T> transform) throws ExecutionException;

    <T extends WritableByteChannel> Output<T> linkOutputChannel(int index, Supplier<T> cstor) throws ExecutionException;

    <T> Output<T> linkOutputStream(int index, Output.Stream<T> transform) throws ExecutionException;
    
    Collection<Output> outputs();
    
    @Override
    void close();
    
}
