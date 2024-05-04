package driver.weaver;

import java.util.*;
import java.io.*;
import java.util.concurrent.atomic.AtomicBoolean;

import java.util.logging.Level;
import java.util.logging.Logger;

public class BaseWeaverConnection implements AutoCloseable {
    
    private final Logger   logging;
    
    private final Throwable creationPath = new Throwable();

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
    
    private final long nativePointer;
    private final AtomicBoolean isOpen = new AtomicBoolean(true);
    
    public int resultField = 0;
    String errorText = "";
    String state = "";
    
    private Statement currentStatement;
    
    private BaseWeaverConnection(long nativePointer, Logger logger) {
        this.nativePointer = nativePointer;
        this.logging = logger;
    }

    private BaseWeaverConnection(String username, String password, String database, Logger parent) {
        nativePointer = connectToDatabaseWithUsername(username, password, database);
        logging = parent == null ? Logger.getLogger("BaseWeaverConnection") : parent;
    }
    
    private BaseWeaverConnection(String db, Logger parent) {
        nativePointer = connectToDatabaseAnonymously(db);
        logging = parent == null ? Logger.getLogger("BaseWeaverConnection") : parent;
    }
    
    public static BaseWeaverConnection connectAnonymously(String db, Logger logger) {
        BaseWeaverConnection connect = new BaseWeaverConnection(db, logger);
        if (connect.isValid()) {
            return connect;
        } else {
            try {
                connect.dispose();
            } catch (WeaverException ee) {
                logger.log(Level.WARNING, "Error disposing connection", ee);
            }
            return null;
        }
    }
    
    public static BaseWeaverConnection connectUser(String username, String password, String database, Logger logger) {
        BaseWeaverConnection connect = new BaseWeaverConnection(username, password, database, logger);
        if (connect.isValid()) {
            return connect;
        } else {
            try {
                connect.dispose();
            } catch (WeaverException ee) {
                logger.log(Level.WARNING, "Error disposing connection", ee);
            }
            return null;
        }
    }
    
    public boolean isValid() {
        return isOpen.get();
    }

    private long connectToDatabaseAnonymously(String db) {
        try {
            return grabConnection(null, null, db);
        } catch (WeaverException ee) {
            return 0L;
        }
    }
    
    private long connectToDatabaseWithUsername(String name, String password, String database) {
        try {
            return grabConnection(name, password, database);
        } catch (WeaverException ee) {
            return 0L;
        }
    }

    @Override
    public void close() throws WeaverException {
        closeCurrentStatement();
        dispose();
    }

    public synchronized void dispose() throws WeaverException {
        if ( nativePointer != 0 && isOpen.compareAndSet(true, false)) {
            dispose(nativePointer);
        }
    }
    
    @Override
    protected void finalize() throws Throwable {
        super.finalize();
        if (isOpen.get()) {
            logging.log(Level.WARNING, "disposal in finalize ", creationPath);
            dispose();
        }
    }

    public BaseWeaverConnection spawnHelper() throws WeaverException {
        return new BaseWeaverConnection(this.connectSubConnection(), this.logging);
    }

    public String idDatabaseRoots() {
        return "Weaver";
    }

    public void clearResult() {
        resultField = 0;
    }
    
    public Statement statement(String statement) throws WeaverException {
        return new Statement(statement);
    }

    public long begin() throws WeaverException {
        closeCurrentStatement();
        return beginTransaction();
    }

    public void prepare() throws WeaverException {
        prepareTransaction();
    }

    public void cancel() throws WeaverException {
        cancelTransaction();
    }

    public void abort() {
        try {
            abortTransaction();
        } catch ( WeaverException exp ) {
            throw new RuntimeException(exp);
        }
        closeCurrentStatement();
    }

    public void commit() throws WeaverException {
        commitTransaction();
        closeCurrentStatement();
    }

    public void start() throws WeaverException {
        beginProcedure();
    }

    public void end() throws WeaverException {
        endProcedure();
    }
    
    public long parse(String stmt) throws WeaverException {
        currentStatement = new Statement(stmt);
        return 0L;
    }
    
    public long execute() throws WeaverException {
        return currentStatement.execute();
    }

    public boolean fetch() throws WeaverException {
        return currentStatement.fetch();
    }
    
    public long getCommandId() {
        return currentStatement.getCommandId();
    }
    
    private void closeCurrentStatement() {
        if (currentStatement != null) {
            currentStatement.close();
            currentStatement = null;
        }
    }
    private native long grabConnection(String name, String password, String connect) throws WeaverException;
    private native long connectSubConnection() throws WeaverException;
    private native void dispose(long link);

    private native long prepareStatement(String theStatement) throws WeaverException;
    private native long executeStatement(long link, BoundInput[] args) throws WeaverException;
    private native boolean fetchResults(long link, BoundOutput[] args) throws WeaverException;

    private native void prepareTransaction() throws WeaverException;
    private native void cancelTransaction();
    private native long beginTransaction() throws WeaverException;
    private native void commitTransaction() throws WeaverException;
    private native void abortTransaction() throws WeaverException;
    private native void beginProcedure() throws WeaverException;
    private native void endProcedure() throws WeaverException;

    public native long getTransactionId();
    public native long getCommandId(long link);

    public native void streamExec(String statement) throws WeaverException;

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
        private final long  link;
        private final AtomicBoolean isOpen = new AtomicBoolean(true);
        private final Throwable statementPath;
        
        Statement(String statement) throws WeaverException {
            link = prepareStatement(statement);
            statementPath = new Throwable(creationPath);
        }

        public BaseWeaverConnection getConnection() {
            return BaseWeaverConnection.this;
        }
        
        private final Map<Integer,BoundOutput> outputs = new HashMap<>();
        private final Map<String,BoundInput> inputs = new HashMap<>();
    
        public <T> BoundOutput<T> linkOutput(int index, Class<T> type)  throws WeaverException {
            BoundOutput<T> bo = outputs.get(index);
            if ( bo != null ) {
                if ( bo.isSameType(type) ) return bo;
                else bo.deactivate();
            }

            bo = new BoundOutput<>(BaseWeaverConnection.this,link,index, type);
            outputs.put(index, bo);
            return bo;
        }
        
        public <T> BoundInput<T> linkInput(String name, Class<T> type)  throws WeaverException {
            BoundInput bi = inputs.get(name);
            if ( bi != null ) {
                if ( bi.isSameType(type) ) return bi;
                else bi.deactivate();
            }

            bi = new BoundInput<>(BaseWeaverConnection.this,link, name, type);
            inputs.put(name, bi);
            return bi;
        }
        
        public boolean fetch() throws WeaverException {
            return fetchResults(link, outputs.values().toArray(BoundOutput[]::new));
        }        
        
        public long execute() throws WeaverException {
            return executeStatement(link, inputs.values().toArray(BoundInput[]::new));
        }
        
        public boolean isValid() {
            return link != 0 && isOpen.get();
        }
        
        public long getCommandId() {
            return BaseWeaverConnection.this.getCommandId(link);
        }

        @Override
        public void close() {
            dispose();
        }

        public void dispose() {
            if (link != 0 && isOpen.compareAndSet(true, false) && BaseWeaverConnection.this.isValid()) {
                BaseWeaverConnection.this.dispose(link);
            }
        }
        
        @Override
        protected void finalize() throws Throwable {
            super.finalize();
            if (isOpen.get()) {
                logging.log(Level.WARNING, "disposal in finalize ", statementPath);
                this.dispose();
            }
        }
    }
}
