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
import java.util.stream.Collectors;

public class BaseWeaverConnection implements Connection {
    
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
    
    static BaseWeaverConnection connectAnonymously(String db) {
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
    
    static BaseWeaverConnection connectUser(String username, String password, String database) {
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
                LOGGING.log(Level.FINE, "disposing unclosed connection");
                disposeConnection(ref.link);
            }
            ref = (ConnectionRef)connections.poll();
        }
    }
    
    @Override
    public boolean isValid() {
        return nativePointer != 0 && isOpen.get();
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

    private  BaseWeaverConnection spawnHelper() throws ExecutionException {
        return new BaseWeaverConnection(this.connectSubConnection());
    }
    
    @Override
    public Connection helper() throws ExecutionException {
        return spawnHelper();
    }

    public String idDatabaseRoots() {
        return "Weaver";
    }

    public void clearResult() {
        resultField = 0;
    }

    @Override
    public long begin() throws ExecutionException {
        if (transactionId == 0) {
            transactionId = beginTransaction();
            return transactionId;
        } else {
            throw new ExecutionException("transaction already active");
        }
    }

    @Override
    public void prepare() throws ExecutionException {
        prepareTransaction();
    }

    @Override
    public void cancel() throws ExecutionException {
        cancelTransaction();
    }

    @Override
    public void abort() {
        try {
            abortTransaction();
        } catch ( ExecutionException exp ) {
            throw new RuntimeException(exp);
        } finally {
            transactionId = 0;
        }
    }

    @Override
    public void commit() throws ExecutionException {
        try {
            commitTransaction();
        } finally {
            transactionId = 0;
        }
    }

    @Override
    public void start() throws ExecutionException {
        beginProcedure();
    }

    @Override
    public void end() throws ExecutionException {
        endProcedure();
    }
    
    @Override
    public Statement statement(String stmt) throws ExecutionException {
        StatementRef ref = (StatementRef)statements.poll();
        while (ref != null) {
            disposeStatement(ref.link);
            ref = (StatementRef)statements.poll();
        }
        
        WeaverStatement s = new WeaverStatement(stmt);
        liveStatements.put(s.link, new StatementRef(s, statements));
        return s;
    }
    
    @Override
    public void stream(String stmt) throws ExecutionException {
        StatementRef ref = (StatementRef)statements.poll();
        while (ref != null) {
            disposeStatement(ref.link);
            ref = (StatementRef)statements.poll();
        }
        streamExec(stmt);
    }
    
    @Override
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
                LOGGING.log(Level.FINE, "disposing unclosed statement");
                dispose(link);
            }
        }
    }
    
    @Override
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

    private native void streamExec(String statement) throws ExecutionException;

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

    @Override
    public void setStandardOutput(OutputStream out) {
        os = out;
    }

    @Override
    public void setStandardInput(InputStream in) {
        is = in;
    }
    
    public class WeaverStatement implements Statement {
        private final long  link;
        private final String raw;
        private boolean executed = false;

        WeaverStatement(String statement) throws ExecutionException {
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
            
        @Override
        public <T> Output<T> linkOutput(int index, Class<T> type)  throws ExecutionException {
            BoundOutput<T> bo = new BoundOutput<>(this,index, type);
            Optional.ofNullable(outputs.put(index, bo)).ifPresent(BoundOutput::deactivate);
            return new Output(bo);
        }
        
        @Override
        public <T> Input<T> linkInput(String name, Class<T> type)  throws ExecutionException {
            BoundInput<T> bi = new BoundInput<>(this, name, type);
            Optional.ofNullable(inputs.put(name, bi)).ifPresent(BoundInput::deactivate);
            return new Input<>(bi);
        }
        
        @Override
        public <T> Input<T> linkInputChannel(String name, Input.Channel<T> transform) throws ExecutionException {
            BoundInputChannel<T> channel = new BoundInputChannel<>(this, transformer, name, transform);
            Optional.ofNullable(inputs.put(name, channel)).ifPresent(BoundInput::deactivate);
            return new Input<>(channel);
        }
        
        @Override
        public <T> Input<T> linkInputStream(String name, Input.Stream<T> transform) throws ExecutionException {
            return linkInputChannel(name, (T value,WritableByteChannel w)->transform.transform(value, Channels.newOutputStream(w)));
        }
        
        @Override
        public <T> Output<T> linkOutputChannel(int index, Output.Channel<T> transform) throws ExecutionException {
            BoundOutputChannel<T> channel = new BoundOutputChannel<>(this, transformer, index, transform);
            Optional.ofNullable(outputs.put(index, channel)).ifPresent(BoundOutput::deactivate);
            return new Output<>(channel);
        }
        
        @Override
        public <T> Output<T> linkOutputStream(int index, Output.Stream<T> transform) throws ExecutionException {
            return linkOutputChannel(index, (src) -> transform.transform(Channels.newInputStream(src)));
        }
        
        @Override
        public <T extends WritableByteChannel> Output<T> linkOutputChannel(int index, Supplier<T> cstor) throws ExecutionException {
            BoundOutputReceiver<T> receiver = new BoundOutputReceiver<>(this, index, cstor);
            Optional.ofNullable(outputs.put(index, receiver)).ifPresent(BoundOutput::deactivate);
            return new Output<>(receiver);
        }
        
        @Override
        public boolean fetch() throws ExecutionException {
            if (!executed) {
                execute();
            }
            for (BoundOutput out : outputs.values()) {
                out.reset();
            }
            return fetchResults(link, outputs.values().toArray(BoundOutput[]::new));
        }
        
        @Override
        public Collection<Output> outputs() {
            return outputs.values().stream().map(Output::new).collect(Collectors.toList());
        }
        
        @Override
        public long execute() throws ExecutionException {
            long processed = 0;
            
            try {
                processed = executeStatement(link, inputs.values().toArray(BoundInput[]::new));
            } finally {
                executed = true;
            }

            return processed;
        }
        
        @Override
        public boolean isValid() {
            return link != 0 && isOpen.get();
        }
        
        @Override
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

        public StatementRef(WeaverStatement referent, ReferenceQueue<? super Statement> q) {
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
