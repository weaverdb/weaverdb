

package org.weaverdb;

import java.util.concurrent.Callable;
import java.util.concurrent.Future;

/**
 *
 * @author myronscott
 */
public interface StreamingTransformer extends AutoCloseable {

    @Override
    public void close();
    
    <T> Future<T> schedule(Callable<T> work);
}
