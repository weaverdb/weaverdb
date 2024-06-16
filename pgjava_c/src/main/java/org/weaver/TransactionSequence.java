/*

 */
package org.weaver;

/**
 *
 * @author myronscott
 */
public class TransactionSequence implements AutoCloseable {
    private final Connection connection;
    private final long current;

    public TransactionSequence(Connection connection) throws ExecutionException {
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
                connection.prepare();
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
