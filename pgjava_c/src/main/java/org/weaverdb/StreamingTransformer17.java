/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/Classes/Class.java to edit this template
 */
package org.weaverdb;

import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/**
 *
 * @author myronscott
 */
public class StreamingTransformer17 implements StreamingTransformer {
    private final ExecutorService vpool = Executors.newCachedThreadPool();

    @Override
    public void close()  {
        vpool.shutdown();
    }
    
    @Override
    public <T> Future<T> schedule(Callable<T> work) {
        return vpool.submit(work);
    }
}
