package driver.weaver;

import java.util.*;
import java.io.*;

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

    public BaseWeaverConnection() {
    }

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

    public synchronized void dispose() throws SQLException {
        if ( id!= null ) dispose(id);
        id = null;
    }
    
    @Override
    protected void finalize() throws Throwable {
        super.finalize();
        dispose();
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
    
    public Statement statement(String statement) throws SQLException {
        return new Statement(statement);
    }

    HashMap<Integer,BoundOutput> outputs = new HashMap<Integer,BoundOutput>();
    
    public <T> BoundOutput<T> linkOutput(int index, Class<T> type)  throws SQLException {
        BoundOutput bo = outputs.get(index);
        if ( bo != null ) {
            if ( bo.isSameType(type) ) return bo;
            else bo.deactivate();
        }

        bo = new BoundOutput<T>(this,id, index, type);
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

        bi = new BoundInput<T>(this,id, name, type);
        inputs.put(name, bi);
        return bi;
    }

    private void clearBinds() {
        for ( BoundInput bi : inputs.values() ) bi.deactivate();
        inputs.clear();
        for ( BoundOutput bo : outputs.values() ) bo.deactivate();
        outputs.clear();
    }

    public long parse(String stmt) throws SQLException {
        clearBinds();
        return parseStatement(stmt);
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

    public void abort() {
        clearBinds();
        try {
            abortTransaction();
        } catch ( SQLException exp ) {
            throw new RuntimeException(exp);
        }
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
        return executeStatement(id);
    }

    public boolean fetch() throws SQLException {
        return fetchResults(id);
    }
    
    private native boolean grabConnection(String name, String password, String connect) throws SQLException;
    private native void connectSubConnection(BaseWeaverConnection parent) throws SQLException;
    private native void dispose(Object link);

    private native Object prepareStatement(String theStatement) throws SQLException;
    private native long parseStatement(String theStatement) throws SQLException;
    private native long executeStatement(Object link) throws SQLException;
    private native boolean fetchResults(Object link) throws SQLException;

    native void setInput(Object link, String name, int type, Object value) throws SQLException;
    native void getOutput(Object link, int index, int type, BoundOutput test) throws SQLException;

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
    
    public class Statement {
        Object  link;
        
        Statement(String statement) throws SQLException {
            link = prepareStatement(statement);
        }

        public BaseWeaverConnection getConnection() {
            return BaseWeaverConnection.this;
        }
        
        HashMap<Integer,BoundOutput> outputs = new HashMap<Integer,BoundOutput>();
        HashMap<String,BoundInput> inputs = new HashMap<String,BoundInput>();
    
        public <T> BoundOutput<T> linkOutput(int index, Class<T> type)  throws SQLException {
            BoundOutput bo = outputs.get(index);
            if ( bo != null ) {
                if ( bo.isSameType(type) ) return bo;
                else bo.deactivate();
            }

            bo = new BoundOutput<T>(BaseWeaverConnection.this,link,index, type);
            outputs.put(index, bo);
            return bo;
        }
        
        public <T> BoundInput<T> linkInput(String name, Class<T> type)  throws SQLException {
            BoundInput bi = inputs.get(name);
            if ( bi != null ) {
                if ( bi.isSameType(type) ) return bi;
                else bi.deactivate();
            }

            bi = new BoundInput<T>(BaseWeaverConnection.this,link, name, type);
            inputs.put(name, bi);
            return bi;
        }
        
        public boolean fetch() throws SQLException {
            return fetchResults(link);
        }        
        
        public long execute() throws SQLException {
            return executeStatement(link);
        }
        
        public void dispose() {
            synchronized (BaseWeaverConnection.this) {
                if ( id != null && link != null ) BaseWeaverConnection.this.dispose(link);
                link = null;
            }
        }
        
        @Override
        protected void finalize() throws Throwable {
            super.finalize();
            this.dispose();
        }
    }
}
