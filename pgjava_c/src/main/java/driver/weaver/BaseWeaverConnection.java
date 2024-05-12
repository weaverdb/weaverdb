package driver.weaver;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.ref.PhantomReference;
import java.lang.ref.ReferenceQueue;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;

import java.util.logging.Level;
import java.util.logging.Logger;

public class BaseWeaverConnection implements AutoCloseable {
    
    private static final Logger   LOGGING = Logger.getLogger("Connection");
    
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
    
    private long transactionId;
    private boolean auto = false;
    int resultField = 0;
    String errorText = "";
    String state = "";
    
    private final Map<Long, StatementRef> liveStatements = new ConcurrentHashMap<>();
    private static final Map<Long, ConnectionRef> liveConnections = new ConcurrentHashMap<>();
    private final ReferenceQueue<Statement> statements = new ReferenceQueue<>();
    private static final ReferenceQueue<BaseWeaverConnection> connections = new ReferenceQueue<>();
        
    private BaseWeaverConnection(long nativePointer) {
        this.nativePointer = nativePointer;
    }

    private BaseWeaverConnection(String username, String password, String database) {
        nativePointer = connectToDatabaseWithUsername(username, password, database);
    }
    
    private BaseWeaverConnection(String db) {
        nativePointer = connectToDatabaseAnonymously(db);
    }
    
    public static BaseWeaverConnection connectAnonymously(String db) {
        closeDiscardedConnections();
        BaseWeaverConnection connect = new BaseWeaverConnection(db);
        if (connect.isValid()) {
            liveConnections.put(connect.nativePointer, new ConnectionRef(connect, connections));
            return connect;
        } else {
            try {
                connect.dispose();
            } catch (WeaverException ee) {
                LOGGING.log(Level.WARNING, "Error disposing connection", ee);
            }
            return null;
        }
    }
    
    public static BaseWeaverConnection connectUser(String username, String password, String database) {
        closeDiscardedConnections();
        BaseWeaverConnection connect = new BaseWeaverConnection(username, password, database);
        if (connect.isValid()) {
            liveConnections.put(connect.nativePointer, new ConnectionRef(connect, connections));
            return connect;
        } else {
            try {
                connect.dispose();
            } catch (WeaverException ee) {
                LOGGING.log(Level.WARNING, "Error disposing connection", ee);
            }
            return null;
        }
    }
    
    private static void closeDiscardedConnections() {
        ConnectionRef ref = (ConnectionRef)connections.poll();
        while (ref != null) {
            ref = liveConnections.remove(ref.link);
            if (ref != null && ref.dispose()) {
                disposeConnection(ref.link);
            }
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
        dispose();
    }

    private synchronized void dispose() throws WeaverException {
        if ( nativePointer != 0 && isOpen.compareAndSet(true, false)) {
            ConnectionRef ref = liveConnections.remove(nativePointer);
            if (ref != null && ref.dispose()) {
                disposeConnection(nativePointer);
            }
        }
    }

    public BaseWeaverConnection spawnHelper() throws WeaverException {
        return new BaseWeaverConnection(this.connectSubConnection());
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
        transactionId = beginTransaction();
        auto = false;
        return transactionId;
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
        } finally {
            transactionId = 0;
        }
    }

    public void commit() throws WeaverException {
        try {
            commitTransaction();
        } finally {
            transactionId = 0;
        }
    }

    public void start() throws WeaverException {
        beginProcedure();
    }

    public void end() throws WeaverException {
        endProcedure();
    }
    
    public Statement parse(String stmt) throws WeaverException {
        if (transactionId == 0) {
            transactionId = beginTransaction();
            auto = true;
        } else if (auto) {
            commitTransaction();
            transactionId = beginTransaction();
        }
        StatementRef ref = (StatementRef)statements.poll();
        while (ref != null) {
            disposeStatement(ref.link);
            ref = (StatementRef)statements.poll();
        }
        
        Statement s = new Statement(stmt);
        liveStatements.put(s.link, new StatementRef(s, statements));
        return s;
    }
    
    private synchronized void disposeStatement(long link) {
        if (isValid()) {
            StatementRef ref = liveStatements.remove(link);
            if (ref != null && ref.dispose()) {
                dispose(link);
            }
        }
    }
    
    public Object transaction() {
        long t = getTransactionId();
        assert(t == transactionId);
        return transactionId;
    }
    
    private native long grabConnection(String name, String password, String connect) throws WeaverException;
    private native long connectSubConnection() throws WeaverException;
    private static native void disposeConnection(long link);
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

    private native long getTransactionId();
    private native long getCommandId(long link);

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
        private final String raw;
        
        Statement(String statement) throws WeaverException {
            link = prepareStatement(statement);
            raw = statement;
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
        
        public long command() {
            return BaseWeaverConnection.this.getCommandId(link);
        }

        @Override
        public void close() {
            dispose();
        }

        private void dispose() {
            if (link != 0) {
                disposeStatement(link);
            }
        }
        
        public long getIdentity() {
            return link;
        }

        @Override
        public String toString() {
            return raw;
        }
    }
    
    private static class StatementRef extends PhantomReference<Statement> {
        
        private final long link;
        private final AtomicBoolean disposed = new AtomicBoolean(false);

        public StatementRef(Statement referent, ReferenceQueue<? super Statement> q) {
            super(referent, q);
            link = referent.link;
        }
        
        public boolean dispose() {
            return disposed.compareAndSet(false, true);
        }
    }
    
    private static class ConnectionRef extends PhantomReference<BaseWeaverConnection> {
        
        private final long link;
        private final AtomicBoolean disposed = new AtomicBoolean(false);

        public ConnectionRef(BaseWeaverConnection referent, ReferenceQueue<? super BaseWeaverConnection> q) {
            super(referent, q);
            link = referent.nativePointer;
        }
        
        public boolean dispose() {
            return disposed.compareAndSet(false, true);
        }
    }    
}
