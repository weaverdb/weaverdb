package driver.weaver;

import java.util.*;
import java.io.*;
import java.lang.reflect.*;
import java.sql.SQLException;

public class WeaverConnection extends WeaverFrameConnection {
    /* only 50 pipes available in native code */
    int next = 0;
    BinaryStream[] streams = new BinaryStream[50];
    HashMap<String,Integer> bindstreams = new HashMap<String,Integer>();
    HashMap<Integer,Integer> outputstreams = new HashMap<Integer,Integer>();
    
    public String 				connectString = "";
    public String				callWords = null;    
    
    public WeaverConnection() {
        
    }
    
    public void grabConnection(String name, String password, String connect) throws SQLException {
        clearResult();
        super.grabConnection(name,password,connect);
    }
    
    public WeaverConnection createSubConnection() throws SQLException {
        WeaverConnection sub = new WeaverConnection();
        
        sub.connectSubConnection(this);
        
        return sub;
    }

    public void outputLink(int index, int theType, Object theRef,
            String slot, String sig, String classname) throws SQLException {
        if ( theType == this.bindStream ) {
            Integer id = outputstreams.get(index);
            /* the first two streams are stdin and stdout */
            if ( !(theRef instanceof OutputStream) ) {
                throw new SQLException(theRef.getClass().getName() + " is incompatible with streaming");
            }
            if ( id != null ) {
                streams[id] = new BinaryStream(id,theRef);
                return;
            }
            streams[next] = new BinaryStream(next, theRef);
            slot = Integer.toString(streams[next].getId());
            theRef = new Integer(streams[next].getId());
            sig = "<stream>";
            classname = "<stream>";
            outputstreams.put(index,next++);
        } else if ( theType == this.bindDirect ) {
            Integer id = outputstreams.get(index);
            if ( !(theRef instanceof OutputStream) && !(theRef instanceof java.nio.channels.WritableByteChannel) ) {
                throw new SQLException(theRef.getClass().getName() + " is incompatible with direct streaming");
            }
            /* the first two streams are stdin and stdout */
            if ( id != null ) {
                streams[id] = new BinaryStream(id,theRef);
                return;
            }
            streams[next] = new BinaryStream(next, theRef);
            slot = Integer.toString(streams[next].getId());
            theRef = new Integer(streams[next].getId());
            sig = "<direct>";
            classname = "<direct>";
            outputstreams.put(index,next++);
        }
        
        super.outputLink(index,theType,theRef, slot, sig, classname);
    }
    
    public void setBind(String which, Object thePass, String slot, int  theType) throws SQLException {
        if ( theType == this.bindStream ) {
            if ( !(thePass instanceof InputStream) ) {
                throw new SQLException(thePass.getClass().getName() + " is incompatible with streaming");
            }
            Integer id = bindstreams.get(which);
            if ( id != null ) {
                streams[id] = new BinaryStream(id,thePass);
                return;
            }
            streams[next] = new BinaryStream(next, thePass);
            slot = Integer.toString(streams[next].getId());
            thePass = new Integer(streams[next].getId());
            bindstreams.put(which,next++);
        } else if ( theType == this.bindDirect ) {
            if ( !(thePass instanceof InputStream) && !(thePass instanceof java.nio.channels.ReadableByteChannel) ) {
                throw new SQLException(thePass.getClass().getName() + " is incompatible with direct streaming");
            }
            Integer id = bindstreams.get(which);
            if ( id != null ) {
                streams[id] = new BinaryStream(id,thePass);
                return;
            }
            streams[next] = new BinaryStream(next, thePass);
            slot = Integer.toString(streams[next].getId());
            thePass = new Integer(streams[next].getId());
            bindstreams.put(which,next++);
        }
        super.setBind(which,thePass, slot, theType);
    }
    
    public void parseStatement(String theStatement) throws SQLException {
        if ( Thread.interrupted() ) throw new SQLException("interrupted");
        
        for (int x=0;x<50;x++) streams[x] = null;
        
        next = 0;
        bindstreams.clear();
        outputstreams.clear();
       
        super.parseStatement(theStatement);
    }
    
    public void execute()  throws SQLException {
        if ( Thread.interrupted() ) throw new SQLException("interrupted");
        super.execute();
    }
    
    public void fetch()  throws SQLException {
        if ( Thread.interrupted() ) throw new SQLException("interrupted");
        super.fetch();
    }
    
 /*  the first two streams 
  * are stdout and stdin so the
  * fist indexes stream is 3
  */
    
    protected Object getStream(int slot) {
        return streams[slot - 2].getStream();
    }
    
    protected void setStream(int slot, Object stream) {
        streams[slot - 2].setStream(stream);
    }
    
    
    class BinaryStream {
        Object stream;
        int streamid;
        int mark;
        
        public BinaryStream(int id,Object s) throws SQLException {
            stream = s;
            streamid = id + 2;
        }
        
        public InputStream getInputStream() {
            if (stream instanceof InputStream ) return (InputStream)stream;
            else return null;
        }
        
        public OutputStream getOutputStream() {
            if (stream instanceof OutputStream ) return (OutputStream)stream;
            else return null;
        }
        
        public Object getStream() {
            return stream;
        }
        
        public void setStream(Object s) {
            stream = s;
        }
        
        public int getId() {
            return streamid;
        }
    }
}
