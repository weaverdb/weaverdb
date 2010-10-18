package org.mtpgsql.server;


import java.io.*;
import java.net.*;
import java.sql.*;

public class Connection implements Runnable {
    public LinkID id = new LinkID();

    int err = 0;
    
    String errorText = "";
    String state = "";
    
    boolean connected;
    boolean done;
    
    DataInputStream datainput;
    DataOutputStream dataoutput;
    Socket netsocket;
    
    SimpleServer server;
    
    public Connection(SimpleServer simple,Socket sock)
 {
     server = simple;
     netsocket = sock;
    }
    
    public void closeConnection()
 {
     try {
         datainput.close();
     dataoutput.close();
     netsocket.close();
     } catch ( Exception exp ) {
         exp.printStackTrace();
     }
     
    }
    
    public boolean buildConnection() throws IOException {
        connected = false;
        
        InputStream input = netsocket.getInputStream();
        datainput = new DataInputStream(input);
        OutputStream output = netsocket.getOutputStream();
        dataoutput = new DataOutputStream(output);
        
        int splen = datainput.readInt();
        int majorver = datainput.readShort();
        int minorver = datainput.readShort();

        
        byte[] db = new byte[64];
        int amount = input.read(db);
        while ( amount != db.length ) {
            int add = 0;
            add += input.read(db,amount,db.length - amount);
            if ( add< 0 ) throw new IOException("connection closed");
            amount += add;
        }
        String database = new String(db).trim();
        
        byte[] user = new byte[32];
        byte[] options = new byte[64];
        byte[] unused = new byte[64];
        byte[] ttyb = new  byte[64];
        
        if ( input.read(user) != user.length ) throw new IOException("startup packet error -- username");
        if ( input.read(options) != options.length ) throw  new IOException("startup packet error -- options");
        if ( input.read(unused) != unused.length ) throw  new IOException("startup packet error -- unused");
        if ( input.read(ttyb) != ttyb.length ) throw  new IOException("startup packet error -- tty");
        
        String username = new String(user).trim();
        
        String opts = new String(options).trim();
        
        String tty = new String(ttyb).trim();
        
        //  send back a request for password
        dataoutput.writeByte('R');
        dataoutput.writeInt(3);
        dataoutput.flush();
        //  receive password
        int passlen = datainput.readInt();
        
        
        byte[] pass = new byte[passlen - 4];
        if ( input.read(pass) != pass.length )  throw  new IOException("startup packet error -- password");
        String password = new String(pass).trim();
        
        String connectid = username + "/" + password + "@" + database;

        try {
            grabConnection(username,password,database);

                        
            //  send ok done
            dataoutput.writeByte('R');
            dataoutput.writeInt(0);
            dataoutput.flush();
            connected = true;
            dataoutput.writeByte('K');
            dataoutput.writeInt(0);
            dataoutput.writeInt(0);
            dataoutput.flush();
            
            Thread.currentThread().setName(username + "@" + netsocket.getInetAddress());
        } catch ( SQLException exp ) {
             dataoutput.writeByte('E');
            dataoutput.flush();
            byte[] errmg = new byte[4096];
            String mess = "User authentication failed";
            System.arraycopy(mess.getBytes(),0,errmg,0,mess.length());
            output.write(errmg);
            output.flush();
            connected = false;
        }
        
        return connected;
        
    }
    
    public String readCommand(DataInputStream dos) throws IOException {
        StringBuffer buffer = new StringBuffer();
        int val = dos.read();
        while ( val >= 0 && val != '\0' ) {
            buffer.append((char)val);
            val = dos.read();
        }
        if ( val < 0 ) throw new IOException("EOF");
        return buffer.toString();
    }
    
    public boolean processCommand() throws IOException {
        DataInputStream dis = datainput;
        DataOutputStream dos = dataoutput;
        boolean fast = false;
        
        String nextcommand = "";
        int result = '?';
        
        try {
            dos.writeByte('Z');
            dos.flush();
        } catch (Exception exp) {
            /*  unknown error, do not continue */
            return false;
        }
        
        char val = '?';
        while ( val == '?' ) {
            try {
                val = (char)dis.readByte();
            } catch ( InterruptedIOException ioe ) {
                if ( !server.isRunning() ) return false;
                val = '?';
            }
        }
        switch ( val ) {
            case 'Q':
                result = 'Q';
                nextcommand = readCommand(dis);

                try {
                    streamExec(nextcommand);
                } catch (SQLException exp ) {
                    dos.writeByte('E');
                    dos.writeBytes(exp.getMessage());
                    dos.writeByte('\0');
                    dos.flush();
                }
                
                break;
            case 'F':
                result = 'F';
                nextcommand = readCommand(dis);
                try {
                    streamExec(nextcommand);
                } catch ( SQLException exp ) {
                    dos.writeByte('E');
                    dos.write(exp.getMessage().getBytes());
                    dos.writeByte('\0');
                }
                break;
            case 'X':
                result = 'X';
                done = true;
                break;
            default:
                throw new java.io.IOException("unknown command");
        }
        if ( !server.isRunning() ) done = true;
        return !done;
    }
    
    public native void grabConnection(String name, String password, String connect) throws SQLException;
    public native void streamExec(String statement) throws SQLException;
    
    protected void pipeOut(byte[] data) throws IOException {
        dataoutput.write(data);
        dataoutput.flush();
    }
    
    protected int pipeIn(byte[] data) throws IOException {
        int count = datainput.read(data,0,data.length);
        return count;
    }
    
    public int getResults()
 {
      return err;
    }
    
    public void run() {
       try {
           netsocket.setSoTimeout(1000);
           if ( buildConnection() ) {
            boolean cont = true;
            while ( cont ) {
                cont = processCommand();
            }
            closeConnection();
        }
       } catch ( IOException ioe ) {
           System.out.println("I/O Exception -- " + ioe.getMessage());
       }
            
    }
    
}
