package driver.weaver;

import java.util.*;
import java.io.*;

import java.util.logging.Level;
import java.util.logging.Logger;

public class BaseWeaverConnection implements AutoCloseable {
    
    private final Logger   logging;
    
    private Throwable creationPath;

    public final static int bindString = 2;
    public final static int bindDouble = 3;
    public final static int bindInteger = 1;
    public final static int bindBinary = 6;
    public final static int bindBLOB = 7;
    public final static int bindCharacter = 4;
    public final static int bindBoolean = 5;
    public final static int bindDate = 12;
    public final static int bindLong = 13;
    public final static int bindFunction = 20;
    public final static int bindSlot = 30;
    public final static int bindJava = 40;
    public final static int bindText = 41;
    public final static int bindStream = 42;
    public final static int bindDirect = 43;
    public final static int bindNull = 0;
    
    private long nativePointer = 0;
    
    public int resultField = 0;
    String errorText = "";
    String state = "";

    public BaseWeaverConnection() {
        logging = Logger.getGlobal();
    }
    
    public BaseWeaverConnection(Logger parent) {
        logging = parent;
    }

    private boolean convertString(String connect) throws ExecutionException {
        if ( connect == null ) return false;
        if ( connect.startsWith("@") ) connect = "/" + connect;
        int userbr = connect.indexOf('/');
         if ( userbr < 0 ) throw new ExecutionException("Connect string is improperly formatted.  Use <username>/<password>@<server>");
        int passbr = connect.indexOf('@',userbr);
        if ( passbr < 0 ) throw new ExecutionException("Connect string is improperly formatted.  Use <username>/<password>@<server>");

        String name = connect.substring(0, userbr);
        String password = connect.substring(userbr+1, passbr);
        String connection = connect.substring(passbr + 1);

        nativePointer = grabConnection(name, password, connection);
        return ( nativePointer != 0 );
    }

    public boolean connect(String connString) throws ExecutionException {
        creationPath = new Throwable();
        return convertString(connString);
    }

    @Override
    public void close() throws ExecutionException {
        dispose();
    }

    public synchronized void dispose() throws ExecutionException {
        if ( nativePointer != 0 ) {
            dispose(nativePointer);
            nativePointer = 0;
        }
    }
    
    @Override
    protected void finalize() throws Throwable {
        super.finalize();
        synchronized (this) {
            if (nativePointer != 0) {
                logging.log(Level.WARNING, "disposal in finalize ", creationPath);
                dispose();
            }
        }
    }

    public BaseWeaverConnection spawnHelper() throws ExecutionException {
        BaseWeaverConnection cloned = new BaseWeaverConnection();
        cloned.nativePointer = cloned.connectSubConnection();
        return cloned;
    }

    public String idDatabaseRoots() {
        return "Postgres";
    }

    public void clearResult() {
        resultField = 0;
        clearBinds();
    }
    
    public Statement statement(String statement) throws ExecutionException {
        return new Statement(statement);
    }

    HashMap<Integer,BoundOutput> outputs = new HashMap<Integer,BoundOutput>();
    
    public <T> BoundOutput<T> linkOutput(int index, Class<T> type)  throws ExecutionException {
        BoundOutput bo = outputs.get(index);
        if ( bo != null ) {
            if ( bo.isSameType(type) ) return bo;
            else bo.deactivate();
        }

        bo = new BoundOutput<T>(this,nativePointer, index, type);
        outputs.put(index, bo);
        return bo;
    }

    HashMap<String,BoundInput> inputs = new HashMap<String,BoundInput>();

    public <T> BoundInput<T> linkInput(String name, Class<T> type)  throws ExecutionException {
        BoundInput bi = inputs.get(name);
        if ( bi != null ) {
            if ( bi.isSameType(type) ) return bi;
            else bi.deactivate();
        }

        bi = new BoundInput<T>(this,nativePointer, name, type);
        inputs.put(name, bi);
        return bi;
    }

    private void clearBinds() {
        for ( BoundInput bi : inputs.values() ) bi.deactivate();
        inputs.clear();
        for ( BoundOutput bo : outputs.values() ) bo.deactivate();
        outputs.clear();
    }

    public long parse(String stmt) throws ExecutionException {
        clearBinds();
        return parseStatement(stmt);
    }

    public long begin() throws ExecutionException {
        clearBinds();
        return beginTransaction();
    }

    public void prepare() throws ExecutionException {
        prepareTransaction();
    }

    public void cancel() throws ExecutionException {
        cancelTransaction();
    }

    public void abort() {
        clearBinds();
        try {
            abortTransaction();
        } catch ( ExecutionException exp ) {
            throw new RuntimeException(exp);
        }
    }

    public void commit() throws ExecutionException {
        clearBinds();
        commitTransaction();
    }

    public void start() throws ExecutionException {
        clearBinds();
        beginProcedure();
    }

    public void end() throws ExecutionException {
        clearBinds();
        endProcedure();
    }

    public long execute() throws ExecutionException {
        return executeStatement(nativePointer);
    }

    public boolean fetch() throws ExecutionException {
        return fetchResults(nativePointer);
    }
    
    private native long grabConnection(String name, String password, String connect) throws ExecutionException;
    private native long connectSubConnection() throws ExecutionException;
    private native void dispose(long link);

    private native long prepareStatement(String theStatement) throws ExecutionException;
    private native long parseStatement(String theStatement) throws ExecutionException;
    private native long executeStatement(long link) throws ExecutionException;
    private native boolean fetchResults(long link) throws ExecutionException;

    native void setInput(long link, String name, int type, Object value) throws ExecutionException;
    native void getOutput(long link, int index, int type, BoundOutput test) throws ExecutionException;

    private native void prepareTransaction() throws ExecutionException;
    private native void cancelTransaction();
    private native long beginTransaction() throws ExecutionException;
    private native void commitTransaction() throws ExecutionException;
    private native void abortTransaction() throws ExecutionException;
    private native void beginProcedure() throws ExecutionException;
    private native void endProcedure() throws ExecutionException;

    public native long getTransactionId();
    public native long getCommandId();

    public native void streamExec(String statement) throws ExecutionException;

    protected void pipeOut(byte[] data) throws IOException {
        os.write(data);
        os.flush();
    }

    protected void pipeOut(java.nio.ByteBuffer data) throws IOException {
        byte[] send = (data.hasArray()) ? data.array() : new byte[data.remaining()];
        if (!data.hasArray()) {
            data.get(send);
        }
        pipeOut(send);
    }

    protected int pipeIn(byte[] data) throws IOException {
        int count = is.read(data, 0, data.length);
        return count;
    }

    protected int pipeIn(java.nio.ByteBuffer data) throws IOException {
        byte[] send = (data.hasArray()) ? data.array() : new byte[data.remaining()];
        if (!data.hasArray()) {
            data.get(send);
        }
        return pipeIn(send);
    }
    
    OutputStream os;
    InputStream is;

    public void setStandardOutput(OutputStream out) {
        os = out;
    }

    public void setStandardInput(InputStream in) {
        is = in;
    }
    
    public class Statement implements AutoCloseable {
        private long  link;
        private final Throwable statementPath;
        
        Statement(String statement) throws ExecutionException {
            link = prepareStatement(statement);
            statementPath = new Throwable(creationPath);
        }

        public BaseWeaverConnection getConnection() {
            return BaseWeaverConnection.this;
        }
        
        HashMap<Integer,BoundOutput> outputs = new HashMap<Integer,BoundOutput>();
        HashMap<String,BoundInput> inputs = new HashMap<String,BoundInput>();
    
        public <T> BoundOutput<T> linkOutput(int index, Class<T> type)  throws ExecutionException {
            BoundOutput bo = outputs.get(index);
            if ( bo != null ) {
                if ( bo.isSameType(type) ) return bo;
                else bo.deactivate();
            }

            bo = new BoundOutput<T>(BaseWeaverConnection.this,link,index, type);
            outputs.put(index, bo);
            return bo;
        }
        
        public <T> BoundInput<T> linkInput(String name, Class<T> type)  throws ExecutionException {
            BoundInput bi = inputs.get(name);
            if ( bi != null ) {
                if ( bi.isSameType(type) ) return bi;
                else bi.deactivate();
            }

            bi = new BoundInput<T>(BaseWeaverConnection.this,link, name, type);
            inputs.put(name, bi);
            return bi;
        }
        
        public boolean fetch() throws ExecutionException {
            return fetchResults(link);
        }        
        
        public long execute() throws ExecutionException {
            return executeStatement(link);
        }

        @Override
        public void close() {
            dispose();
        }

        public void dispose() {
            synchronized (BaseWeaverConnection.this) {
                if ( link != 0 && nativePointer != 0 ) {
                    BaseWeaverConnection.this.dispose(link);
                }
                link = 0;
            }
        }
        
        @Override
        protected void finalize() throws Throwable {
            super.finalize();
            synchronized (BaseWeaverConnection.this) {
                if (link != 0) {
                    logging.log(Level.WARNING, "disposal in finalize ", statementPath);
                }
            }
            this.dispose();
        }
    }
}