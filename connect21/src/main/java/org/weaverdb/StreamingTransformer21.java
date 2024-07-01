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
public class StreamingTransformer21 implements StreamingTransformer {
    
    ExecutorService vpool = Executors.newVirtualThreadPerTaskExecutor();

    @Override
    public void close() {
        vpool.shutdownNow();
    }

    @Override
    public <T> Future<T> schedule(Callable<T> work) {
        return vpool.submit(work);
    }
    
}
