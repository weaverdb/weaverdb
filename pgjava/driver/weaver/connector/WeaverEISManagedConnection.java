/*
 * WeaverEISManagedConnection.java
 *
 * Created on October 22, 2006, 8:41 AM
 *
 * To change this template, choose Tools | Template Manager
 * and open the template in the editor.
 */

package driver.weaver.connector;

import driver.weaver.WeaverConnection;
import java.io.PrintWriter;
import javax.resource.ResourceException;
import javax.resource.spi.ConnectionEventListener;
import javax.resource.spi.ConnectionRequestInfo;
import javax.resource.spi.LocalTransaction;
import javax.resource.spi.ManagedConnection;
import javax.resource.spi.ManagedConnectionMetaData;
import javax.resource.spi.ResourceAdapter;
import javax.resource.spi.ResourceAdapterAssociation;
import javax.security.auth.Subject;
import javax.transaction.xa.XAResource;

/**
 *
 * @author mscott
 */
public class WeaverEISManagedConnection implements ManagedConnection, ResourceAdapterAssociation {
    WeaverConnection base = new WeaverConnection();
    
    /** Creates a new instance of WeaverEISManagedConnection */
    public WeaverEISManagedConnection(Subject sub, ConnectionRequestInfo request) {
        base.grabConnection(
    }

    public Object getConnection(Subject subject, ConnectionRequestInfo connectionRequestInfo) throws ResourceException {
        
    }

    public void destroy() throws ResourceException {
    }

    public void cleanup() throws ResourceException {
    }

    public void associateConnection(Object object) throws ResourceException {
    }

    public void addConnectionEventListener(ConnectionEventListener connectionEventListener) {
    }

    public void removeConnectionEventListener(ConnectionEventListener connectionEventListener) {
    }

    public XAResource getXAResource() throws ResourceException {
    }

    public LocalTransaction getLocalTransaction() throws ResourceException {
    }

    public ManagedConnectionMetaData getMetaData() throws ResourceException {
    }

    public void setLogWriter(PrintWriter printWriter) throws ResourceException {
    }

    public PrintWriter getLogWriter() throws ResourceException {
    }

    public ResourceAdapter getResourceAdapter() {
    }

    public void setResourceAdapter(ResourceAdapter resourceAdapter) throws ResourceException {
    }

    
}
