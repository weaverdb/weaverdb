/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/Classes/Interface.java to edit this template
 */
package driver.weaver;

import java.io.InputStream;
import java.io.OutputStream;
import java.util.Optional;
import java.util.ServiceLoader;

/**
 *
 * @author myronscott
 */
public interface Connection extends AutoCloseable {
    
    static Optional<ConnectionFactory> loader = ServiceLoader.load(ConnectionFactory.class).findFirst();

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
    
    Connection helper() throws ExecutionException;
    
    static Connection connectAnonymously(String db) {
        return loader.orElseThrow().connectAnonymousy(db);
    }
    
    static Connection connectUser(String username, String password, String database) {
        return loader.orElseThrow().connectUser(username, password, database);
    }
}
