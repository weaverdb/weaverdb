package driver.weaver;

import java.util.*;
import java.io.*;
import java.lang.reflect.*;
import java.nio.channels.*;

import java.sql.SQLException;

public class BaseWeaverConnection {

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
    public LinkID id = new LinkID();
    
    public int resultField = 0;
    String errorText = "";
    String state = "";
    
    String current_stmt = null;
    long stmt_id;
    /*
    private StreamingType streamingPersonality = StreamingType.STREAMING_STREAMS;
    private int streaming_size = 64 * 1024;

    public enum StreamingType {

        STREAMING_STREAMS(0x00, 0x00),
        DIRECT_STREAMS(0x01, 0x00),
        STREAMING_CHANNELS(0x00, 0x01),
        DIRECT_CHANNELS(0x01, 0x01);
        private int tt;
        private int bt;

        StreamingType(int transferType, int bufferType) {
            tt = transferType;
            bt = bufferType;
        }

        public boolean isTransferTypeStreamed() {
            return (tt == 0x00);
        }

        public boolean isBufferTypeStreams() {
            return (bt == 0x00);
        }
    }
*/
    public BaseWeaverConnection() {
    }
/*
    public void setStreamingPersonality(StreamingType type) {
        streamingPersonality = type;
    }

    public void setStreamingBufferSize(int size) {
        streaming_size = size;
    }
*/
    private boolean convertString(String connect) throws SQLException {
        if ( connect == null ) return false;
        if ( connect.startsWith("@") ) connect = "/" + connect;
        int userbr = connect.indexOf('/');
         if ( userbr < 0 ) throw new SQLException("Connect string is improperly formatted.  Use <username>/<password>@<server>");
        int passbr = connect.indexOf('@',userbr);
        if ( passbr < 0 ) throw new SQLException("Connect string is improperly formatted.  Use <username>/<password>@<server>");

        String name = connect.substring(0, userbr);
        String password = connect.substring(userbr+1, passbr);
        String connection = connect.substring(passbr + 1);

        return grabConnection(name, password, connection);
    }

    public boolean connect(String connString) throws SQLException {
        return convertString(connString);
    }

    public void dispose() throws SQLException {
        disposeConnection();
    }

    public BaseWeaverConnection spawnHelper() throws SQLException {
        BaseWeaverConnection cloned = new BaseWeaverConnection();
        cloned.connectSubConnection(this);
        return cloned;
    }

    public String idDatabaseRoots() {
        return "Postgres";
    }

    public void clearResult() {
        resultField = 0;
        clearBinds();
    }

    HashMap<Integer,BoundOutput> outputs = new HashMap<Integer,BoundOutput>();
    
    public <T> BoundOutput<T> linkOutput(int index, Class<T> type)  throws SQLException {
        BoundOutput bo = outputs.get(index);
        if ( bo != null ) {
            if ( bo.isSameType(type) ) return bo;
            else bo.deactivate();
        }

        bo = new BoundOutput<T>(this, index, type);
        getOutput(index,bo.getTypeId(),bo);
        outputs.put(index, bo);
        return bo;
    }

    HashMap<String,BoundInput> inputs = new HashMap<String,BoundInput>();

    public <T> BoundInput<T> linkInput(String name, Class<T> type)  throws SQLException {
        BoundInput bi = inputs.get(name);
        if ( bi != null ) {
            if ( bi.isSameType(type) ) return bi;
            else bi.deactivate();
        }

        bi = new BoundInput<T>(this, name, type);
        inputs.put(name, bi);
        return bi;
    }

    private void clearBinds() {
        for ( BoundInput bi : inputs.values() ) bi.deactivate();
        inputs.clear();
        for ( BoundOutput bo : outputs.values() ) bo.deactivate();
        outputs.clear();
        current_stmt = null;
    }
    
    public String getParsedStatement() {
        return current_stmt;
    }

    public long parse(String stmt) throws SQLException {
        clearBinds();
        current_stmt = stmt;
        stmt_id = parseStatement(stmt);
        return stmt_id;
    }
    
    public long parseIfNew(String stmt) throws SQLException {
        if ( stmt.equals(current_stmt) ) return stmt_id;
        return parse(stmt);
    }

    public long begin() throws SQLException {
        clearBinds();
        return beginTransaction();
    }

    public void prepare() throws SQLException {
        prepareTransaction();
    }

    public void cancel() throws SQLException {
        cancelTransaction();
    }

    public void abort() throws SQLException {
        clearBinds();
        abortTransaction();
    }

    public void commit() throws SQLException {
        clearBinds();
        commitTransaction();
    }

    public void start() throws SQLException {
        clearBinds();
        beginProcedure();
    }

    public void end() throws SQLException {
        clearBinds();
        endProcedure();
    }

    public long execute() throws SQLException {
        return executeStatement();
    }

    public boolean fetch() throws SQLException {
        return fetchResults();
    }
    
    private native boolean grabConnection(String name, String password, String connect) throws SQLException;
    private native void connectSubConnection(BaseWeaverConnection parent) throws SQLException;
    private native void disposeConnection();

    private native long parseStatement(String theStatement) throws SQLException;
    private native long executeStatement() throws SQLException;
    private native boolean fetchResults() throws SQLException;

    native void setInput(String name, int type, Object value) throws SQLException;
    native void getOutput(int index, int type, BoundOutput test) throws SQLException;

//    public native void userLock(String group, int connector, boolean lock) throws SQLException;
    private native void prepareTransaction() throws SQLException;
    private native void cancelTransaction();
    private native long beginTransaction() throws SQLException;
    private native void commitTransaction() throws SQLException;
    private native void abortTransaction() throws SQLException;
    private native void beginProcedure() throws SQLException;
    private native void endProcedure() throws SQLException;

    public native long getTransactionId();
    public native long getCommandId();

    public native void streamExec(String statement) throws SQLException;

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
}
