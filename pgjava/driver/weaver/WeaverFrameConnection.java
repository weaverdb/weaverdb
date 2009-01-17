package driver.weaver;

import java.util.*;
import java.io.*;
import java.lang.reflect.*;
import java.nio.channels.*;

import java.sql.SQLException;

public abstract class WeaverFrameConnection {

    public LinkID id = new LinkID();
    protected boolean valid = false;
    public int openConnection = 0;
    public int resultField = 0;
    String errorText = "";
    String state = "";
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

    public WeaverFrameConnection() {

    }

    public String getErrorText() {
        return errorText.trim();
    }

    private void convertString(String connect) throws SQLException {
        String name = connect.substring(0, connect.indexOf('/'));
        String password = connect.substring(connect.indexOf('/') + 1, connect.indexOf('@'));
        String connection = connect.substring(connect.indexOf('@') + 1);

        grabConnection(name, password, connection);
    }

    public void grabConnection(String connString) throws SQLException {
        convertString(connString);
    }

    public String idDatabaseRoots() {
        return "Postgres";
    }

    public void clearResult() {
        resultField = 0;
    }

    public int getResult() {
        return resultField;
    }

    public native void grabConnection(String name, String password, String connect) throws SQLException;

    public native void connectSubConnection(WeaverFrameConnection parent) throws SQLException;

    public native void parseStatement(String theStatement) throws SQLException;

    public native void bind(String number, int type) throws SQLException;

    public native void setBind(String which, Object thePass, String slot, int theType) throws SQLException;

    public native void outputLink(int index, int theType, Object theRef,
            String slot, String sig, String classname) throws SQLException;

    public native void execute() throws SQLException;

    public native void fetch() throws SQLException;

    public native void disposeConnection();

    public native void prepare() throws SQLException;

    public native void userLock(String group, int connector, boolean lock) throws SQLException;

    public native void beginTransaction() throws SQLException;

    public native void commitTransaction() throws SQLException;

    public native void abortTransaction() throws SQLException;

    public native void beginProcedure() throws SQLException;

    public native void endProcedure() throws SQLException;

    public native long getTransactionId();

    public native void cancel();

    public native void streamExec(String statement) throws SQLException;

    public void outputLink(int index, Object theRef, String slot) throws SQLException {
        int type = -1;
        try {
            String cName = theRef.getClass().getName().replace('.', '/');
            String sig = "";

            if (theRef instanceof java.io.OutputStream) {
                sig = "<assigned>";
                type = WeaverFrameConnection.bindStream;
            } else {
                Class f = theRef.getClass().getDeclaredField(slot).getType();
                String s = f.getName();


                if (s.endsWith("Integer") || s.equals("int")) {
                    type = WeaverFrameConnection.bindInteger;
                    sig = "I";
                } else if (s.endsWith("String")) {
                    type = WeaverFrameConnection.bindString;
                    sig = "Ljava/lang/String;";
                } else if (s.equals("char")) {
                    sig = "C";
                    type = WeaverFrameConnection.bindCharacter;
                } else if (s.endsWith("Array") || s.equals("[B")) {
                    sig = "[B";
                }
            }
            /*   now call outputLink     */
            if (type != -1) {
                outputLink(index, type, theRef, slot, sig, cName);
            } else {
                throw new NoSuchFieldException("bad outputlink");
            }
        } catch (NoSuchFieldException exp) {
            SQLException se = new SQLException(exp.getMessage());
            throw se;
        }
    }

    protected abstract Object getStream(int slot);

    protected void pipeOut(int streamid, byte[] data) throws IOException {
        if (streamid == 1) {
            os.write(data);
            os.flush();
        } else {
            OutputStream outs = (OutputStream) getStream(streamid);
            outs.write(data);
        }
    }

    protected int pipeIn(int streamid, byte[] data) throws IOException {
        if (streamid == 0) {
            int count = is.read(data, 0, data.length);
            return count;
        } else {
            InputStream ins = (InputStream) getStream(streamid);
            int count = ins.read(data, 0, data.length);
            return count;
        }
    }

    protected void pipeOut(int streamid, java.nio.ByteBuffer data) throws IOException {
        if (streamid == 1) {
            byte[] send = (data.hasArray()) ? data.array() : new byte[data.remaining()];
            if (!data.hasArray()) {
                data.get(send);
            }

            pipeOut(streamid, send);
        }

        Object outs = getStream(streamid);
        if (outs instanceof WritableByteChannel) {
            ((WritableByteChannel) outs).write(data);
        } else {
            byte[] open = new byte[data.limit()];
            data.get(open);
            ((OutputStream) outs).write(open);
        }
    }

    protected int pipeIn(int streamid, java.nio.ByteBuffer data) throws IOException {
        if (streamid == 0) {
            byte[] send = (data.hasArray()) ? data.array() : new byte[data.remaining()];
            if (!data.hasArray()) {
                data.get(send);
            }

            pipeIn(streamid, send);
        }

        Object ins = getStream(streamid);
        if (ins instanceof ReadableByteChannel) {
            int move = ((ReadableByteChannel) ins).read(data);
            return move;
        } else {
            byte[] open = new byte[data.limit()];
            int count = ((InputStream) ins).read(open, 0, open.length);
            try {
                if ( count > 0 ) data.put(open, 0, count);
            } catch (RuntimeException run) {
                throw run;
            }
            return count;

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
}
