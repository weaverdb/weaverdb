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

    public BaseWeaverConnection() {
    }

    public void setStreamingPersonality(StreamingType type) {
        streamingPersonality = type;
    }

    public void setStreamingBufferSize(int size) {
        streaming_size = size;
    }

    private boolean convertString(String connect) throws SQLException {
        String name = connect.substring(0, connect.indexOf('/'));
        String password = connect.substring(connect.indexOf('/') + 1, connect.indexOf('@'));
        String connection = connect.substring(connect.indexOf('@') + 1);

        return grabConnection(name, password, connection);
    }

    public boolean grabConnection(String connString) throws SQLException {
        return convertString(connString);
    }

    public String idDatabaseRoots() {
        return "Postgres";
    }

    public void clearResult() {
        resultField = 0;
    }
    
    public <T> BoundOutput<T> linkOutput(int index, Class<T> type)  throws SQLException {
        return new BoundOutput(this,index, type);
    }
    
    public <T> BoundInput<T> linkInput(String name, Class<T> type)  throws SQLException {
        return new BoundInput(this,name, type);
    }
    
    public native boolean grabConnection(String name, String password, String connect) throws SQLException;

    public native void connectSubConnection(BaseWeaverConnection parent) throws SQLException;

    public native long parseStatement(String theStatement) throws SQLException;

    native void bind(String name, int type) throws SQLException;

    native void setBind(String name, Object value) throws SQLException;

    native void output(int index, BoundOutput target, int type) throws SQLException;

    public native long execute() throws SQLException;

    public native boolean fetch() throws SQLException;

    public native void disposeConnection();

    public native void prepare() throws SQLException;

    public native void userLock(String group, int connector, boolean lock) throws SQLException;

    public native long beginTransaction() throws SQLException;

    public native void commitTransaction() throws SQLException;

    public native void abortTransaction() throws SQLException;

    public native void beginProcedure() throws SQLException;

    public native void endProcedure() throws SQLException;

    public native long getTransactionId();
    public native long getCommandId();

    public native void cancel();

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
