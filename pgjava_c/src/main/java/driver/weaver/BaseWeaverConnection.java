package driver.weaver;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.ref.PhantomReference;
import java.lang.ref.ReferenceQueue;
import java.nio.channels.Channels;
import java.nio.channels.WritableByteChannel;
import java.util.Collection;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Supplier;

import java.util.logging.Level;
import java.util.logging.Logger;

public class BaseWeaverConnection implements AutoCloseable {
    
    private static final Logger   LOGGING = Logger.getLogger("Connection");
    
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
    private final StreamingTransformer transformer = new StreamingTransformer();
    
    private long transactionId;
    private boolean allowAutoCommit = true;
    
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
            } catch (ExecutionException ee) {
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
            } catch (ExecutionException ee) {
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
        } catch (ExecutionException ee) {
            return 0L;
        }
    }
    
    private long connectToDatabaseWithUsername(String name, String password, String database) {
        try {
            return grabConnection(name, password, database);
        } catch (ExecutionException ee) {
            return 0L;
        }
    }

    @Override
    public void close() throws ExecutionException {
        transformer.close();
        dispose();
    }

    private synchronized void dispose() throws ExecutionException {
        if ( nativePointer != 0 && isOpen.compareAndSet(true, false)) {
            ConnectionRef ref = liveConnections.remove(nativePointer);
            if (ref != null && ref.dispose()) {
                disposeConnection(nativePointer);
            }
        }
    }

    public BaseWeaverConnection spawnHelper() throws ExecutionException {
        return new BaseWeaverConnection(this.connectSubConnection());
    }

    public String idDatabaseRoots() {
        return "Weaver";
    }

    public void clearResult() {
        resultField = 0;
    }

    public long begin() throws ExecutionException {
        if (transactionId == 0) {
            transactionId = beginTransaction();
            return transactionId;
        } else {
            throw new ExecutionException("transaction already active");
        }
    }

    public void prepare() throws ExecutionException {
        prepareTransaction();
    }

    public void cancel() throws ExecutionException {
        cancelTransaction();
    }

    public void abort() {
        try {
            abortTransaction();
        } catch ( ExecutionException exp ) {
            throw new RuntimeException(exp);
        } finally {
            transactionId = 0;
        }
    }

    public void commit() throws ExecutionException {
        try {
            commitTransaction();
        } finally {
            transactionId = 0;
        }
    }

    public void start() throws ExecutionException {
        beginProcedure();
    }

    public void end() throws ExecutionException {
        endProcedure();
    }
    
    public Statement statement(String stmt) throws ExecutionException {
        StatementRef ref = (StatementRef)statements.poll();
        while (ref != null) {
            disposeStatement(ref.link);
            ref = (StatementRef)statements.poll();
        }
        
        Statement s = new Statement(stmt);
        liveStatements.put(s.link, new StatementRef(s, statements));
        return s;
    }
    
    public long execute(String statement) throws ExecutionException {
        long result;
        try (Statement s = statement(statement)) {
            result = s.execute();
        }
        return result;
    }
    
    private synchronized void disposeStatement(long link) {
        if (isValid()) {
            StatementRef ref = liveStatements.remove(link);
            if (ref != null && ref.dispose()) {
                dispose(link);
            }
        }
    }
    
    private long checkForTransaction(long current) throws ExecutionException {
        if (transactionId == 0 && allowAutoCommit) {
            LOGGING.info("using auto-commit");
            return begin();
        } else {
            return current;
        }
    }
    
    private void checkForAutoCommit(long currentCommit) throws ExecutionException {
        if (transactionId == currentCommit && allowAutoCommit) {
            LOGGING.info("committing by auto-commit");
            commit();
        }
    }
    
    public long transaction() {
        if (transactionId == 0) {
            return getTransactionId();
        } else {
            return transactionId;
        }
    }
    
    private native long grabConnection(String name, String password, String connect) throws ExecutionException;
    private native long connectSubConnection() throws ExecutionException;
    private static native void disposeConnection(long link);
    private native void dispose(long link);

    private native long prepareStatement(String theStatement) throws ExecutionException;
    private native long executeStatement(long link, BoundInput[] args) throws ExecutionException;
    private native boolean fetchResults(long link, BoundOutput[] args) throws ExecutionException;

    private native void prepareTransaction() throws ExecutionException;
    private native void cancelTransaction();
    private native long beginTransaction() throws ExecutionException;
    private native void commitTransaction() throws ExecutionException;
    private native void abortTransaction() throws ExecutionException;
    private native void beginProcedure() throws ExecutionException;
    private native void endProcedure() throws ExecutionException;

    private native long getTransactionId();
    private native long getCommandId(long link);

    public native void streamExec(String statement) throws ExecutionException;

    private final int pipeOut(byte[] data) throws IOException {
        if (os != null) {
            os.write(data);
            os.flush();
        }
        return data.length;
    }

    private final int pipeIn(byte[] data) throws IOException {
        if (is != null) {
            return is.read(data, 0, data.length);
        } else {
            return data.length;
        }
    }
    
    OutputStream os;
    InputStream is;

    public void setStandardOutput(OutputStream out) {
        os = out;
    }

    public void setStandardInput(InputStream in) {
        is = in;
    }
    
    public void setAutoCommit(boolean auto) {
        this.allowAutoCommit = auto;
    }
    
    public class Statement implements AutoCloseable {
        private final long  link;
        private final String raw;
        private boolean executed = false;
        private long autoCommit;

        Statement(String statement) throws ExecutionException {
            autoCommit = checkForTransaction(autoCommit);
            link = prepareStatement(statement);
            if (link == 0) {
                throw new ExecutionException("statement parsing error");
            }
            raw = statement;
        }

        public BaseWeaverConnection getConnection() {
            return BaseWeaverConnection.this;
        }
        
        private final Map<Integer,BoundOutput> outputs = new HashMap<>();
        private final Map<String,BoundInput> inputs = new HashMap<>();
            
        public <T> Output<T> linkOutput(int index, Class<T> type)  throws ExecutionException {
            BoundOutput<T> bo = new BoundOutput<>(this,index, type);
            Optional.ofNullable(outputs.put(index, bo)).ifPresent(BoundOutput::deactivate);
            return new Output(bo);
        }
        
        public <T> Input<T> linkInput(String name, Class<T> type)  throws ExecutionException {
            BoundInput<T> bi = new BoundInput<>(this, name, type);
            Optional.ofNullable(inputs.put(name, bi)).ifPresent(BoundInput::deactivate);
            return new Input<>(bi);
        }
        
        public <T> Input<T> linkInputChannel(String name, Input.Channel<T> transform) throws ExecutionException {
            BoundInputChannel<T> channel = new BoundInputChannel<>(this, transformer, name, transform);
            Optional.ofNullable(inputs.put(name, channel)).ifPresent(BoundInput::deactivate);
            return new Input<>(channel);
        }
        
        public <T> Input<T> linkInputStream(String name, Input.Stream<T> transform) throws ExecutionException {
            return linkInputChannel(name, (T value,WritableByteChannel w)->transform.transform(value, Channels.newOutputStream(w)));
        }
        
        public <T> Output<T> linkOutputChannel(int index, Output.Channel<T> transform) throws ExecutionException {
            BoundOutputChannel<T> channel = new BoundOutputChannel<>(this, transformer, index, transform);
            Optional.ofNullable(outputs.put(index, channel)).ifPresent(BoundOutput::deactivate);
            return new Output<>(channel);
        }
        
        public <T> Output<T> linkOutputStream(int index, Output.Stream<T> transform) throws ExecutionException {
            return linkOutputChannel(index, (src) -> transform.transform(Channels.newInputStream(src)));
        }
        
        public <T extends WritableByteChannel> Output<T> linkOutputChannel(int index, Supplier<T> cstor) throws ExecutionException {
            BoundOutputReceiver<T> receiver = new BoundOutputReceiver<>(this, index, cstor);
            Optional.ofNullable(outputs.put(index, receiver)).ifPresent(BoundOutput::deactivate);
            return new Output<>(receiver);
        }
        
        public boolean fetch() throws ExecutionException {
            if (!executed) {
                execute();
            }
            for (BoundOutput out : outputs.values()) {
                out.reset();
            }
            return fetchResults(link, outputs.values().toArray(BoundOutput[]::new));
        }
        
        Collection<BoundOutput> outputs() {
            return outputs.values();
        }
        
        public long execute() throws ExecutionException {
            long processed = 0;
            
            autoCommit = checkForTransaction(autoCommit);

            try {
                processed = executeStatement(link, inputs.values().toArray(BoundInput[]::new));
            } finally {
                executed = true;
            }
            if (processed > 0 && autoCommit > 0) {
                checkForAutoCommit(autoCommit);
            }
            return processed;
        }
        
        public boolean isValid() {
            return link != 0 && isOpen.get();
        }
        
        public long command() {
            return BaseWeaverConnection.this.getCommandId(link);
        }

        @Override
        public void close() {
            try {
                checkForAutoCommit(autoCommit);
            } catch (ExecutionException ee) {
                LOGGING.log(Level.WARNING, "failed to auto-commit on close", ee);
            }
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
