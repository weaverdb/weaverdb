/*-------------------------------------------------------------------------
 *
 *
 * Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 *
 * All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.ref.PhantomReference;
import java.lang.ref.ReferenceQueue;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.channels.Channels;
import java.nio.channels.WritableByteChannel;
import java.util.Collection;
import java.util.Comparator;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Supplier;
import java.util.logging.Level;
import java.util.logging.Logger;
import java.util.stream.Collectors;

import static java.lang.foreign.ValueLayout.*;

class DirectWeaverConnection implements DBReference {
    
    private static final Logger   LOGGING = Logger.getLogger("DirectWeaverConnection");

    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup LOADER;

    private static final MethodHandle WCreateConnection;
    private static final MethodHandle WCreateSubConnection;
    private static final MethodHandle WDestroyConnection;
    private static final MethodHandle WDestroyPreparedStatement;
    private static final MethodHandle WPrepareStatement;
    private static final MethodHandle WExec;
    private static final MethodHandle WFetch;
    private static final MethodHandle WPrepare;
    private static final MethodHandle WCommit;
    private static final MethodHandle WBegin;
    private static final MethodHandle WRollback;
    private static final MethodHandle WCancel;
    private static final MethodHandle WBeginProcedure;
    private static final MethodHandle WEndProcedure;
    private static final MethodHandle WGetTransactionId;
    private static final MethodHandle WGetCommandId;
    private static final MethodHandle WStreamExec;
    private static final MethodHandle WGetErrorText;
    private static final MethodHandle WBindTransfer;
    private static final MethodHandle WOutputTransfer;

    static {
        LOADER = SymbolLookup.loaderLookup();

        WCreateConnection = LINKER.downcallHandle(LOADER.find("WCreateConnection").orElseThrow(),
                FunctionDescriptor.of(ADDRESS, ADDRESS, ADDRESS, ADDRESS));
        WCreateSubConnection = LINKER.downcallHandle(LOADER.find("WCreateSubConnection").orElseThrow(),
                FunctionDescriptor.of(ADDRESS, ADDRESS));
        WDestroyConnection = LINKER.downcallHandle(LOADER.find("WDestroyConnection").orElseThrow(),
                FunctionDescriptor.ofVoid(ADDRESS));
        WDestroyPreparedStatement = LINKER.downcallHandle(LOADER.find("WDestroyPreparedStatement").orElseThrow(),
                FunctionDescriptor.ofVoid(ADDRESS));
        WPrepareStatement = LINKER.downcallHandle(LOADER.find("WPrepareStatement").orElseThrow(),
                FunctionDescriptor.of(ADDRESS, ADDRESS, ADDRESS));
        WExec = LINKER.downcallHandle(LOADER.find("WExec").orElseThrow(), FunctionDescriptor.of(JAVA_LONG, ADDRESS));
        WFetch = LINKER.downcallHandle(LOADER.find("WFetch").orElseThrow(), FunctionDescriptor.of(JAVA_LONG, ADDRESS));
        WPrepare = LINKER.downcallHandle(LOADER.find("WPrepare").orElseThrow(), FunctionDescriptor.of(JAVA_INT, ADDRESS));
        WCommit = LINKER.downcallHandle(LOADER.find("WCommit").orElseThrow(), FunctionDescriptor.of(JAVA_INT, ADDRESS));
        WBegin = LINKER.downcallHandle(LOADER.find("WBegin").orElseThrow(), FunctionDescriptor.of(JAVA_LONG, ADDRESS, JAVA_LONG));
        WRollback = LINKER.downcallHandle(LOADER.find("WRollback").orElseThrow(),
                FunctionDescriptor.of(JAVA_INT, ADDRESS));
        WCancel = LINKER.downcallHandle(LOADER.find("WCancel").orElseThrow(), FunctionDescriptor.ofVoid(ADDRESS));
        WBeginProcedure = LINKER.downcallHandle(LOADER.find("WBeginProcedure").orElseThrow(),
                FunctionDescriptor.of(JAVA_INT, ADDRESS));
        WEndProcedure = LINKER.downcallHandle(LOADER.find("WEndProcedure").orElseThrow(),
                FunctionDescriptor.of(JAVA_INT, ADDRESS));
        WGetTransactionId = LINKER.downcallHandle(LOADER.find("WGetTransactionId").orElseThrow(),
                FunctionDescriptor.of(JAVA_LONG, ADDRESS));
        WGetCommandId = LINKER.downcallHandle(LOADER.find("WGetCommandId").orElseThrow(),
                FunctionDescriptor.of(JAVA_LONG, ADDRESS));
        WStreamExec = LINKER.downcallHandle(LOADER.find("WStreamExec").orElseThrow(),
                FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS));
        WGetErrorText = LINKER.downcallHandle(LOADER.find("WGetErrorText").orElseThrow(),
                FunctionDescriptor.of(ADDRESS, ADDRESS));
        WBindTransfer = LINKER.downcallHandle(LOADER.find("WBindTransfer").orElseThrow(),
                FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS, JAVA_INT, ADDRESS, ADDRESS));
        WOutputTransfer = LINKER.downcallHandle(LOADER.find("WOutputTransfer").orElseThrow(),
                FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_SHORT, JAVA_INT, ADDRESS, ADDRESS));
    }
    
    public final static int bindString = 2;
    public final static int bindDouble = 3;
    public final static int bindFloat = 9;
    public final static int bindShort = 8;
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
    
    private final MemorySegment nativePointer;
    private final AtomicBoolean isOpen = new AtomicBoolean(true);
    private final StreamingTransformer transformer;
    
    private long transactionId;
    
    private final Map<MemorySegment, StatementRef> liveStatements = new ConcurrentHashMap<>();
    private static final Map<MemorySegment, ConnectionRef> liveConnections = new ConcurrentHashMap<>();
    private final ReferenceQueue<Statement> statements = new ReferenceQueue<>();
    private static final ReferenceQueue<DirectWeaverConnection> connections = new ReferenceQueue<>();
        
    private DirectWeaverConnection(MemorySegment nativePointer) {
        this.nativePointer = nativePointer;
        this.transformer = null;
    }

    private DirectWeaverConnection(String username, String password, String database, StreamingTransformer version) throws ExecutionException {
        nativePointer = connectToDatabaseWithUsername(username, password, database);
        this.transformer = version;
    }
    
    private DirectWeaverConnection(String db, StreamingTransformer version) throws ExecutionException {
        nativePointer = connectToDatabaseAnonymously(db);
        this.transformer = version;
    }
    
    static DirectWeaverConnection connectAnonymously(String db, StreamingTransformer version) {
        closeDiscardedConnections();
        try {
            DirectWeaverConnection connect = new DirectWeaverConnection(db, version);
            if (connect.isValid()) {
                liveConnections.put(connect.nativePointer, new ConnectionRef(connect, connections));
                return connect;
            } else {
                connect.close();
                return null;
            }
        } catch (ExecutionException ee) {
            LOGGING.log(Level.WARNING, "Error connecting to database", ee);
            return null;
        }
    }
    
    static DirectWeaverConnection connectUser(String username, String password, String database, StreamingTransformer version) {
        closeDiscardedConnections();
        try {
            DirectWeaverConnection connect = new DirectWeaverConnection(username, password, database, version);
            if (connect.isValid()) {
                liveConnections.put(connect.nativePointer, new ConnectionRef(connect, connections));
                return connect;
            } else {
                connect.close();
                return null;
            }
        } catch (ExecutionException ee) {
            LOGGING.log(Level.WARNING, "Error connecting to database", ee);
            return null;
        }
    }
    
    private static void closeDiscardedConnections() {
        ConnectionRef ref = (ConnectionRef)connections.poll();
        while (ref != null) {
            ConnectionRef removed = liveConnections.remove(ref.link);
            if (removed != null && removed.dispose()) {
                LOGGING.log(Level.FINE, "disposing unclosed connection");
                disposeConnection(removed.link);
            }
            ref = (ConnectionRef)connections.poll();
        }
    }
    
    @Override
    public boolean isValid() {
        return !nativePointer.equals(MemorySegment.NULL) && isOpen.get();
    }

    private MemorySegment connectToDatabaseAnonymously(String db) throws ExecutionException {
        return grabConnection(null, null, db);
    }
    
    private MemorySegment connectToDatabaseWithUsername(String name, String password, String database) throws ExecutionException {
        return grabConnection(name, password, database);
    }

    @Override
    public void close() throws ExecutionException {
        if (transformer != null) {
            transformer.close();
        }
        dispose();
    }

    private synchronized void dispose() {
        if (isValid() && isOpen.compareAndSet(true, false)) {
            ConnectionRef ref = liveConnections.remove(nativePointer);
            if (ref != null && ref.dispose()) {
                disposeConnection(nativePointer);
            }
        }
    }

    private  DirectWeaverConnection spawnHelper() throws ExecutionException {
        return new DirectWeaverConnection(this.connectSubConnection());
    }
    
    public DBReference helper() throws ExecutionException {
        return spawnHelper();
    }

    public String idDatabaseRoots() {
        return "Weaver";
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

    public void prepare() throws ExecutionException {
        prepareTransaction();
    }

    @Override
    public void cancel() {
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

        DirectWeaverStatement s = new DirectWeaverStatement(stmt);
        liveStatements.put(s.getIdentity(), new StatementRef(s, statements));
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
    
    private synchronized void disposeStatement(MemorySegment link) {
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
    
    static boolean hasLiveConnections() {
        return !liveConnections.isEmpty();
    }
    
    private MemorySegment grabConnection(String name, String password, String connect) throws ExecutionException {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nameSeg = (name == null) ? MemorySegment.NULL : arena.allocateFrom(name);
            MemorySegment passSeg = (password == null) ? MemorySegment.NULL : arena.allocateFrom(password);
            MemorySegment connSeg = (connect == null) ? MemorySegment.NULL : arena.allocateFrom(connect);
            MemorySegment connection = (MemorySegment) WCreateConnection.invokeExact(nameSeg, passSeg, connSeg);
            if (connection.equals(MemorySegment.NULL)) {
                throw new ExecutionException("could not connect to weaver");
            }
            return connection;
        } catch (Throwable e) {
            throw new ExecutionException(e);
        }
    }

    private MemorySegment connectSubConnection() throws ExecutionException {
        try {
            MemorySegment seg = (MemorySegment) WCreateSubConnection.invokeExact(nativePointer);
            if (seg.equals(MemorySegment.NULL)) {
                throw new ExecutionException("could not create sub connection");
            }
            return seg;
        } catch (Throwable e) {
            throw new ExecutionException(e);
        }
    }

    private static void disposeConnection(MemorySegment link) {
        try {
            WDestroyConnection.invokeExact(link);
        } catch (Throwable e) {
            // ignore
        }
    }

    private void dispose(MemorySegment link) {
        try {
            WDestroyPreparedStatement.invokeExact(link);
        } catch (Throwable e) {
            // ignore
        }
    }

    private MemorySegment prepareStatement(String theStatement) throws ExecutionException {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment stmt = (MemorySegment) WPrepareStatement.invokeExact(nativePointer, arena.allocateFrom(theStatement));
            if (stmt.equals(MemorySegment.NULL)) {
                handleError();
            }
            return stmt;
        } catch (Throwable e) {
            throw new ExecutionException(e);
        }
    }

    private long executeStatement(MemorySegment link, BoundInput<?>[] args) throws ExecutionException {
        try (Arena arena = Arena.ofConfined()) {
            for (BoundInput<?> arg : args) {
                MemorySegment name = arena.allocateFrom(arg.getName());
                
                MemorySegment pipeInHandle = LINKER.upcallStub(
                    MethodHandles.lookup().bind(arg, "pipeIn", MethodType.methodType(int.class, MemorySegment.class, int.class, MemorySegment.class, int.class)),
                    FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT, ADDRESS, JAVA_INT), 
                    arena
                );

                if ((int) WBindTransfer.invokeExact(link, name, arg.getTypeId(), MemorySegment.NULL, pipeInHandle) != 0) {
                    handleError();
                }
            }
            long result = (long) WExec.invokeExact(link);
            if (result < 0) {
                handleError();
            }
            return result;
        } catch (Throwable e) {
            throw new ExecutionException(e);
        }
    }

    private boolean fetchResults(MemorySegment link, BoundOutput<?>[] args) throws ExecutionException {
        try (Arena arena = Arena.ofConfined()) {
            for (BoundOutput<?> arg : args) {
                 MemorySegment pipeOutHandle = LINKER.upcallStub(
                    MethodHandles.lookup().bind(arg, "pipeOut", MethodType.methodType(int.class, MemorySegment.class, int.class, MemorySegment.class, int.class)),
                    FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT, ADDRESS, JAVA_INT), 
                    arena
                );
                
                if ((int) WOutputTransfer.invokeExact(link, (short)arg.getIndex(), arg.getTypeId(), MemorySegment.NULL, pipeOutHandle) != 0) {
                    handleError();
                }
            }
            long result = (long) WFetch.invokeExact(link);
            if (result < 0) {
                handleError();
            }
            return result > 0;
        } catch (Throwable e) {
            throw new ExecutionException(e);
        }
    }

    private void prepareTransaction() throws ExecutionException {
        try {
            if ((int) WPrepare.invokeExact(nativePointer) != 0) {
                handleError();
            }
        } catch (Throwable e) {
            throw new ExecutionException(e);
        }
    }

    private void cancelTransaction() {
        try {
            WCancel.invokeExact(nativePointer);
        } catch (Throwable e) {
            // ignore
        }
    }

    private long beginTransaction() throws ExecutionException {
        try {
            long id = (long) WBegin.invokeExact(nativePointer, 0L);
            if (id == 0) {
                handleError();
            }
            return id;
        } catch (Throwable e) {
            throw new ExecutionException(e);
        }
    }

    private void commitTransaction() throws ExecutionException {
        try {
            if ((int) WCommit.invokeExact(nativePointer) != 0) {
                handleError();
            }
        } catch (Throwable e) {
            throw new ExecutionException(e);
        }
    }

    private void abortTransaction() throws ExecutionException {
        try {
            if ((int) WRollback.invokeExact(nativePointer) != 0) {
                handleError();
            }
        } catch (Throwable e) {
            throw new ExecutionException(e);
        }
    }

    private void beginProcedure() throws ExecutionException {
        try {
            if ((int) WBeginProcedure.invokeExact(nativePointer) != 0) {
                handleError();
            }
        } catch (Throwable e) {
            throw new ExecutionException(e);
        }
    }

    private void endProcedure() throws ExecutionException {
        try {
            if ((int) WEndProcedure.invokeExact(nativePointer) != 0) {
                handleError();
            }
        } catch (Throwable e) {
            throw new ExecutionException(e);
        }
    }

    private long getTransactionId() {
        try {
            return (long) WGetTransactionId.invokeExact(nativePointer);
        } catch (Throwable e) {
            return 0;
        }
    }

    private long getCommandId(MemorySegment link) {
        try {
            return (long) WGetCommandId.invokeExact(link);
        } catch (Throwable e) {
            return 0;
        }
    }

    private void streamExec(String statement) throws ExecutionException {
        try (Arena arena = Arena.ofConfined()) {
            if ((int) WStreamExec.invokeExact(nativePointer, arena.allocateFrom(statement)) != 0) {
                handleError();
            }
        } catch (Throwable e) {
            throw new ExecutionException(e);
        }
    }

    private void handleError() throws ExecutionException {
        try {
            MemorySegment text = (MemorySegment) WGetErrorText.invokeExact(nativePointer);
            if (!text.equals(MemorySegment.NULL)) {
                throw new ExecutionException(text.getString(0));
            }
        } catch (Throwable e) {
            // ignore
        }
        throw new ExecutionException("unknown error");
    }

    private int pipeOut(byte[] data) throws IOException {
        if (os != null) {
            os.write(data);
            os.flush();
        }
        return data.length;
    }

    private int pipeIn(byte[] data) throws IOException {
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
    
    private class DirectWeaverStatement implements Statement {
        private final MemorySegment  link;
        private final String raw;
        private boolean executed = false;
        private boolean closed = false;

        private DirectWeaverStatement(String statement) throws ExecutionException {
            link = prepareStatement(statement);
            if (link.equals(MemorySegment.NULL)) {
                throw new ExecutionException("statement parsing error");
            }
            raw = statement;
        }

        DirectWeaverConnection getConnection() {
            return DirectWeaverConnection.this;
        }
        
        private final Map<Integer,BoundOutput<?>> outputs = new HashMap<>();
        private final Map<String,BoundInput<?>> inputs = new HashMap<>();
            
        @Override
        public <T> Output<T> linkOutput(int index, Class<T> type)  throws ExecutionException {
            BoundOutput<T> bo = new BoundOutput<>(this,index, type);
            Optional.ofNullable(outputs.put(index, bo)).ifPresent(BoundOutput::deactivate);
            return new Output<>(bo);
        }
        
        @Override
        public <T> Input<T> linkInput(String name, Class<T> type)  throws ExecutionException {
            BoundInput<T> bi = new BoundInput<>(this, name, type);
            Optional.ofNullable(inputs.put(name, bi)).ifPresent(BoundInput::deactivate);
            try {
                WBindTransfer.invokeExact(link, bi.getName(), bi.getTypeId(), Arena.ofAuto().allocateFrom(name), MemorySegment.NULL);
            } catch (Throwable t) {
                
            }
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
            if (closed) {
                return false;
            }
            
            for (BoundOutput<?> out : outputs.values()) {
                out.reset();
            }
            return fetchResults(link, outputs.values().toArray(new BoundOutput<?>[0]));
        }
        
        @Override
        public Collection<Output> outputs() {
            return outputs.values().stream().sorted(Comparator.comparingInt(BoundOutput::getIndex)).map(Output::new).collect(Collectors.toList());
        }
        
        @Override
        public Collection<Input> inputs() {
            return inputs.values().stream().map(Input::new).collect(Collectors.toList());
        }
        
        @Override
        public long execute() throws ExecutionException {
            long processed = 0;
            
            try {
                processed = executeStatement(link, inputs.values().toArray(new BoundInput<?>[0]));
            } finally {
                executed = true;
            }

            return processed;
        }
        
        @Override
        public boolean isValid() {
            return !link.equals(MemorySegment.NULL) && isOpen.get();
        }
        
        @Override
        public long command() {
            return DirectWeaverConnection.this.getCommandId(link);
        }

        @Override
        public void close() {
            if (!closed) {
                dispose();
                closed = true;
            }
        }

        private void dispose() {
            if (!link.equals(MemorySegment.NULL)) {
                disposeStatement(link);
            }
        }
        
        public MemorySegment getIdentity() {
            return link;
        }

        @Override
        public String toString() {
            return raw;
        }
    }
    
    private static class StatementRef extends PhantomReference<Statement> {
        
        private final MemorySegment link;
        private final AtomicBoolean disposed = new AtomicBoolean(false);

        public StatementRef(DirectWeaverStatement referent, ReferenceQueue<? super Statement> q) {
            super(referent, q);
            link = referent.link;
        }
        
        public boolean dispose() {
            return disposed.compareAndSet(false, true);
        }
    }
    
    private static class ConnectionRef extends PhantomReference<DirectWeaverConnection> {
        
        private final MemorySegment link;
        private final AtomicBoolean disposed = new AtomicBoolean(false);

        public ConnectionRef(DirectWeaverConnection referent, ReferenceQueue<? super DirectWeaverConnection> q) {
            super(referent, q);
            link = referent.nativePointer;
        }
        
        public boolean dispose() {
            return disposed.compareAndSet(false, true);
        }
    }
}
