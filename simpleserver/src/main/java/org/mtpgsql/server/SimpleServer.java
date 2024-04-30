package org.mtpgsql.server;

import java.io.*;
import java.net.*;
import java.awt.*;
import java.awt.event.*;

public class SimpleServer implements Runnable,ActionListener {
    public  ThreadGroup connections = new ThreadGroup("connections");
    
    ServerSocket server_socket = null;
    boolean running = true;
    
    public SimpleServer(ServerSocket server) {
        server_socket = server;
    }
    
    public static void main(String[] args) {
        Button bt = new Button();
        try {
        java.awt.Frame f = new java.awt.Frame();
        f.setTitle("Multithreaded Postgres SimpleServer");
        f.show();
        f.setLayout(new BorderLayout());
        bt.setLabel("SHUTDOWN");
        f.add(bt,BorderLayout.CENTER);
        f.validate();
        } catch ( Throwable exp ) {
            System.out.println("no display");
        }
        System.loadLibrary("mtpgjava");
       
        try {
            SimpleServer server = new SimpleServer(new ServerSocket(5432));
            server.init();
            bt.addActionListener(server);
            Runtime.getRuntime().addShutdownHook(new Thread(server));
            boolean alive = true;
            
            while ( alive ) {
                server.connect();
            }
        } catch (IOException ioe) {
            ioe.printStackTrace();
        }
    }
    
    public void actionPerformed(ActionEvent ae)
 {
     System.exit(0);
    }
    
    public void connect() throws IOException
 {
    Socket socket = server_socket.accept();
                new Thread(connections,new Connection(this,socket)).start();
    }
    
    public static native void init();
    public static native void close();
    
    public boolean isRunning()
 {
     return running;
    }
    
    public void run() {
        running = false;
        try {
            server_socket.close();
        } catch ( IOException ioe ) {
            ioe.printStackTrace();
        }
        int count = connections.activeCount();
        while ( count > 0 ) {
            Thread[] stopper = new Thread[count];
            connections.enumerate(stopper);
            for(int x=0;x<count;x++ ) {
                if ( stopper[x] != null ) {
                    stopper[x].interrupt();
                    try {
                        stopper[x].join();
                    } catch ( InterruptedException ie ) {
                        ie.printStackTrace();
                    }
                }
                count = connections.activeCount();
            }
        }
        close();
    }
    
}
