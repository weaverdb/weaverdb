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

package org.weaverdb.direct;

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

import static java.lang.foreign.ValueLayout.*;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodHandles.Lookup;
import java.lang.invoke.MethodType;
import java.util.ArrayList;
import java.util.List;
import org.weaverdb.DBReference;
import org.weaverdb.ExecutionException;
import org.weaverdb.Input;
import org.weaverdb.Output;
import org.weaverdb.Statement;
import org.weaverdb.StreamingTransformer;

class DirectWeaverConnection implements DBReference {
    
    private static final Logger   LOGGING = Logger.getLogger("DirectWeaverConnection");

    private static final Linker LINKER = Linker.nativeLinker();

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
    private static final MethodHandle WConnectStdIO;
    private static final MethodHandle WDisconnectStdIO;
    
    private static final MethodHandle PIPEIN;
    private static final MethodHandle PIPEOUT;

    static {
        SymbolLookup LOADER;
        
        LOADER = SymbolLookup.loaderLookup();

        WCreateConnection = LINKER.downcallHandle(LOADER.find("WCreateConnection").orElseThrow(),
                FunctionDescriptor.of(ADDRESS, ADDRESS, ADDRESS, ADDRESS));
        WCreateSubConnection = LINKER.downcallHandle(LOADER.find("WCreateSubConnection").orElseThrow(),
                FunctionDescriptor.of(ADDRESS, ADDRESS));
        WDestroyConnection = LINKER.downcallHandle(LOADER.find("WDestroyConnection").orElseThrow(),
                FunctionDescriptor.of(JAVA_LONG,ADDRESS));
        WDestroyPreparedStatement = LINKER.downcallHandle(LOADER.find("WDestroyPreparedStatement").orElseThrow(),
                FunctionDescriptor.of(JAVA_LONG,ADDRESS));
        WPrepareStatement = LINKER.downcallHandle(LOADER.find("WPrepareStatement").orElseThrow(),
                FunctionDescriptor.of(ADDRESS, ADDRESS, ADDRESS));
        WExec = LINKER.downcallHandle(LOADER.find("WExec").orElseThrow(), FunctionDescriptor.of(JAVA_LONG, ADDRESS));
        WFetch = LINKER.downcallHandle(LOADER.find("WFetch").orElseThrow(), FunctionDescriptor.of(JAVA_LONG, ADDRESS));
        WPrepare = LINKER.downcallHandle(LOADER.find("WPrepare").orElseThrow(), FunctionDescriptor.of(JAVA_LONG, ADDRESS));
        WCommit = LINKER.downcallHandle(LOADER.find("WCommit").orElseThrow(), FunctionDescriptor.of(JAVA_LONG, ADDRESS));
        WBegin = LINKER.downcallHandle(LOADER.find("WBegin").orElseThrow(), FunctionDescriptor.of(JAVA_LONG, ADDRESS, JAVA_LONG));
        WRollback = LINKER.downcallHandle(LOADER.find("WRollback").orElseThrow(),
                FunctionDescriptor.of(JAVA_LONG, ADDRESS));
        WCancel = LINKER.downcallHandle(LOADER.find("WCancel").orElseThrow(), FunctionDescriptor.of(JAVA_LONG, ADDRESS));
        WBeginProcedure = LINKER.downcallHandle(LOADER.find("WBeginProcedure").orElseThrow(),
                FunctionDescriptor.of(JAVA_LONG, ADDRESS));
        WEndProcedure = LINKER.downcallHandle(LOADER.find("WEndProcedure").orElseThrow(),
                FunctionDescriptor.of(JAVA_LONG, ADDRESS));
        WGetTransactionId = LINKER.downcallHandle(LOADER.find("WGetTransactionId").orElseThrow(),
                FunctionDescriptor.of(JAVA_LONG, ADDRESS));
        WGetCommandId = LINKER.downcallHandle(LOADER.find("WGetCommandId").orElseThrow(),
                FunctionDescriptor.of(JAVA_LONG, ADDRESS));
        WStreamExec = LINKER.downcallHandle(LOADER.find("WStreamExec").orElseThrow(),
                FunctionDescriptor.of(JAVA_LONG, ADDRESS, ADDRESS));
        WGetErrorText = LINKER.downcallHandle(LOADER.find("WGetErrorText").orElseThrow(),
                FunctionDescriptor.of(ADDRESS, ADDRESS));
        WBindTransfer = LINKER.downcallHandle(LOADER.find("WBindTransfer").orElseThrow(),
                FunctionDescriptor.of(JAVA_LONG, ADDRESS, ADDRESS, JAVA_INT, ADDRESS, ADDRESS));
        WOutputTransfer = LINKER.downcallHandle(LOADER.find("WOutputTransfer").orElseThrow(),
                FunctionDescriptor.of(JAVA_LONG, ADDRESS, JAVA_SHORT, JAVA_INT, ADDRESS, ADDRESS));
        WConnectStdIO = LINKER.downcallHandle(LOADER.find("WConnectStdIO").orElseThrow(), 
                FunctionDescriptor.ofVoid(ADDRESS, ADDRESS, ADDRESS, ADDRESS));
        WDisconnectStdIO = LINKER.downcallHandle(LOADER.find("WDisconnectStdIO").orElseThrow(), 
                FunctionDescriptor.ofVoid(ADDRESS));

        Lookup LOCAL = MethodHandles.lookup();
        try {
            PIPEIN = LOCAL.findVirtual(DirectWeaverConnection.class, "pipeIn", MethodType.methodType(int.class, MemorySegment.class, int.class, MemorySegment.class, int.class));
            PIPEOUT = LOCAL.findVirtual(DirectWeaverConnection.class, "pipeOut", MethodType.methodType(int.class, MemorySegment.class, int.class, MemorySegment.class, int.class));
        } catch (IllegalAccessException | NoSuchMethodException no) {
            throw new IllegalArgumentException(no);
        }
    }
    
    public final static int  SHORT=	21;
    public final static int  CHAR=	21;
    public final static int  INT=	23;
    public final static int  STRING=	1043;
    public final static int  BOOLEAN=	16;
    public final static int  BYTE=	18;
    public final static int  META=	19;   // column name transfer
    public final static int  BYTEA=       17;
    public final static int  TEXT=         25;
    public final static int  BLOB=         1803;
    public final static int  JAVA=        1830;
    public final static int  TIMESTAMP=    1184;
    public final static int  FLOAT=    700;
    public final static int  DOUBLE=    701;
    public final static int  LONG=    20;
    public final static int  SLOT=    	1901;
    public final static int  STREAM=	1834;
    public final static int GENERIC = 0;
    
    public final static int PIPING_ERROR=  -2;
    public final static int NULL_VALUE=  -1;
    public final static int TRUNCATION_VALUE= -32;
    public final static int CLOSE_OP= -4;
    public final static int LENGTH_QUERY_OP= -8;
    public final static int NULL_CHECK_OP= -16;
    
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
    
    public static boolean hasLiveConnections() {
        closeDiscardedConnections();
        return !liveConnections.isEmpty();
    }
    
    public DBReference helper() throws ExecutionException {
        return new DirectWeaverConnection(this.connectSubConnection());
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
            long result = (long)WDestroyConnection.invokeExact(link);
            if (result != 0) {
                LOGGING.warning("error in dispose:" + result);
            }
        } catch (Throwable t) {
            LOGGING.warning(t.getMessage());
        }
    }

    private void dispose(MemorySegment link) {
        try {
            check((long)WDestroyPreparedStatement.invokeExact(link));
        } catch (ExecutionException ee) {
            LOGGING.warning(ee.getMessage());
        } catch (Throwable t) {
            LOGGING.warning(t.getMessage());
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

    private void prepareTransaction() throws ExecutionException {
        try {
            check((long)WPrepare.invokeExact(nativePointer));
        } catch (ExecutionException ee) {
            throw ee;
        } catch (Throwable t) {
            throw new ExecutionException(t);
        }
    }

    private void cancelTransaction() {
        try {
            check((long)WCancel.invokeExact(nativePointer));
        } catch (ExecutionException ee) {
            LOGGING.warning(ee.getMessage());
        } catch (Throwable t) {
            LOGGING.warning(t.getMessage());
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
            check((long)WCommit.invokeExact(nativePointer));
        } catch (ExecutionException ee) {
            throw ee;
        } catch (Throwable t) {
            throw new ExecutionException(t);
        }
    }

    private void abortTransaction() throws ExecutionException {
        try {
            check((long)WRollback.invokeExact(nativePointer));
        } catch (ExecutionException ee) {
            throw ee;
        } catch (Throwable t) {
            throw new ExecutionException(t);
        }
}

    private void beginProcedure() throws ExecutionException {
        try {
            check((long)WBeginProcedure.invokeExact(nativePointer));
        } catch (ExecutionException ee) {
            throw ee;
        } catch (Throwable t) {
            throw new ExecutionException(t);
        }
    }

    private void endProcedure() throws ExecutionException {
        try {
            check((long)WEndProcedure.invokeExact(nativePointer));
        } catch (ExecutionException ee) {
            throw ee;
        } catch (Throwable t) {
            throw new ExecutionException(t);
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
    
    private final int pipeOut(MemorySegment user, int type, MemorySegment buffer, int len) throws IOException {
        if (os != null) {
            try (Arena a = Arena.ofConfined()) {
                byte[] data = buffer.reinterpret(len).toArray(JAVA_BYTE);
                os.write(data);
                os.flush();
            }
        }
        return len;
    }

    private final int pipeIn(MemorySegment user, int type, MemorySegment buffer, int len) throws IOException {
        if (is != null) {
            try (Arena a = Arena.ofConfined()) {
                byte[] data = new byte[len];
                int read = is.read(data, 0, data.length);
                MemorySegment src = MemorySegment.ofArray(data);
                src = src.reinterpret(read);
                buffer.copyFrom(src);
                return read;
            }
        } else {
            return 0;
        }
    }
    
    private void streamExec(String statement) throws ExecutionException {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment in = LINKER.upcallStub(PIPEIN.bindTo(this), FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT, ADDRESS, JAVA_INT), arena);
            MemorySegment out = LINKER.upcallStub(PIPEOUT.bindTo(this), FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT, ADDRESS, JAVA_INT), arena);
            try {
                check((long)WConnectStdIO.invokeExact(nativePointer, MemorySegment.NULL, in, out));
                check((long)WStreamExec.invokeExact(nativePointer, arena.allocateFrom(statement)));
                check((long)WDisconnectStdIO.invokeExact(nativePointer));
            } catch (ExecutionException ee) {
                throw ee;
            } catch (Throwable t) {
                throw new ExecutionException(t);
            }
        }
    }
    
    private void check(long check) throws ExecutionException {
        long result = (Long)check;
        if (result != 0) {
            handleError();
        }
    }

    private void handleError() throws ExecutionException {
        MemorySegment text = MemorySegment.NULL;
        try {
            text = (MemorySegment)WGetErrorText.invokeExact(nativePointer);
        } catch (Throwable e) {
            throw new ExecutionException(e);
        }
        if (!text.equals(MemorySegment.NULL)) {
            try (Arena a = Arena.ofConfined()) {
                text = text.reinterpret(256, a, null);
                throw new ExecutionException(text.getString(0));
            }
        }
    }
    
    private OutputStream os;
    private InputStream is;

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
        
        private final Map<Integer,DirectOutput<?>> outputs = new HashMap<>();
        private final Map<String,DirectInput<?>> inputs = new HashMap<>();
            
        @Override
        public <T> Output<T> linkOutput(int index, Class<T> type)  throws ExecutionException {
            DirectOutput<T> bo = new DirectOutput(index, type);
            Optional.ofNullable(outputs.put(index, bo)).ifPresent(DirectOutput::deactivate);
            try {
                check((long)WOutputTransfer.invokeExact(link, (short)index, bo.getType(), MemorySegment.NULL, bo.createUpcallStub()));
            } catch (ExecutionException ee) {
                throw ee;
            } catch (Throwable t) {
                throw new ExecutionException(t);
            }
            return new Output<>(bo::getName, bo::get, index);
        }
        
        @Override
        public <T> Input<T> linkInput(String name, Class<T> type)  throws ExecutionException {
            DirectInput<T> bi = new DirectInput(name, type);
            Optional.ofNullable(inputs.put(name, bi)).ifPresent(DirectInput::deactivate);
            try {
                check((long)WBindTransfer.invokeExact(link, name, bi.getType(), MemorySegment.NULL, bi.createUpcallStub()));
            } catch (ExecutionException ee) {
                throw ee;
            } catch (Throwable t) {
                throw new ExecutionException(t);
            }
        return new Input<>(bi::set);
        }
        
        @Override
        public <T> Input<T> linkInputChannel(String name, Input.Channel<T> transform) throws ExecutionException {
            DirectInputChannel<T> channel = new DirectInputChannel<>(transformer, name, transform);
            Optional.ofNullable(inputs.put(name, channel)).ifPresent(DirectInput::deactivate);
            try {
                check((long)WBindTransfer.invokeExact(link, name, channel.getType(), MemorySegment.NULL, channel.createUpcallStub()));
            } catch (ExecutionException ee) {
                throw ee;
            } catch (Throwable t) {
                throw new ExecutionException(t);
            }
            return new Input<>(channel::transform);
        }
        
        @Override
        public <T> Input<T> linkInputStream(String name, Input.Stream<T> transform) throws ExecutionException {
            return linkInputChannel(name, (T value,WritableByteChannel w)->transform.transform(value, Channels.newOutputStream(w)));
        }
        
        @Override
        public <T> Output<T> linkOutputChannel(int index, Output.Channel<T> transform) throws ExecutionException {
            DirectOutputChannel<T> channel = new DirectOutputChannel<>(this, transformer, index, transform);
            Optional.ofNullable(outputs.put(index, channel)).ifPresent(DirectOutput::deactivate);
            try {
                check((long)WOutputTransfer.invokeExact(link, (short)index, channel.getType(), MemorySegment.NULL, channel.createUpcallStub()));
            } catch (ExecutionException ee) {
                throw ee;
            } catch (Throwable t) {
                throw new ExecutionException(t);
            }
            return new Output<>(channel::getName, channel::transform, channel.getIndex());
        }
        
        @Override
        public <T> Output<T> linkOutputStream(int index, Output.Stream<T> transform) throws ExecutionException {
            return linkOutputChannel(index, (src) -> transform.transform(Channels.newInputStream(src)));
        }
        
        @Override
        public <T extends WritableByteChannel> Output<T> linkOutputChannel(int index, Supplier<T> cstor) throws ExecutionException {
            DirectOutputReceiver<T> receiver = new DirectOutputReceiver<>(this, index, cstor);
            Optional.ofNullable(outputs.put(index, receiver)).ifPresent(DirectOutput::deactivate);
            try {
                check((long)WOutputTransfer.invokeExact(link, (short)index, receiver.getType(), MemorySegment.NULL, receiver.createUpcallStub()));
            } catch (ExecutionException ee) {
                throw ee;
            } catch (Throwable t) {
                throw new ExecutionException(t);
            }
            return new Output<>(receiver::getName, receiver::transform, receiver.getIndex());
        }
        
        @Override
        public boolean fetch() throws ExecutionException {
            if (!executed) {
                execute();
            }
            if (closed) {
                return false;
            }
            
            for (DirectOutput<?> out : outputs.values()) {
                out.reset();
            }
            long result = 0;
            try {
                result = (long)WFetch.invokeExact(link);
            } catch (Throwable t) {
                throw new ExecutionException(t);
            }
            
            if (result == -4) { //EOD
                return false;
            } else if (result != 0) {
                handleError();
            } else {
                return true;
            }

            return false;
        }
        
        @Override
        public Collection<Output<?>> outputs() {
            List<Output<?>> send = new ArrayList<>(outputs.size());
            for (DirectOutput out : outputs.values()) {
                send.add(new Output<>(out::getName, out::get, out.getIndex()));
            }
            send.sort((a,b)->Integer.compare(a.getIndex(), b.getIndex()));
            return send;
        }
        
        @Override
        public Collection<Input<?>> inputs() {
            List<Input<?>> send = new ArrayList<>(inputs.size());
            for (DirectInput<?> in : inputs.values()) {
                send.add(new Input<>(in::set));
            }
            return send;
        }
        
        @Override
        public long execute() throws ExecutionException {
            long processed = 0;
            
            try {
                processed = (long)WExec.invokeExact(link);
            } catch (Throwable t) {
                throw new ExecutionException(t);
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
