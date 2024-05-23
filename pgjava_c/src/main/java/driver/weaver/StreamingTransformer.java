

package driver.weaver;

import java.io.IOException;
import java.nio.channels.Pipe;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/**
 *
 * @author myronscott
 */
class StreamingTransformer implements AutoCloseable {
    
    private final ExecutorService vpool = Executors.newVirtualThreadPerTaskExecutor();

    @Override
    public void close()  {
        vpool.shutdown();
    }
    
    <T> Future<T> schedule(Callable<T> work) {
        return vpool.submit(work);
    }
}