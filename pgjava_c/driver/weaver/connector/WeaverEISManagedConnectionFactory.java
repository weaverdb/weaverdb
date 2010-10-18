/*
 * WeaverEISManagedConnectionFactory.java
 *
 * Created on October 22, 2006, 8:59 AM
 *
 * To change this template, choose Tools | Template Manager
 * and open the template in the editor.
 */

package driver.weaver.connector;

import java.io.PrintWriter;
import java.util.Set;
import javax.resource.ResourceException;
import javax.resource.spi.ConnectionManager;
import javax.resource.spi.ConnectionRequestInfo;
import javax.resource.spi.ManagedConnection;
import javax.resource.spi.ManagedConnectionFactory;
import javax.resource.spi.ResourceAdapter;
import javax.resource.spi.ResourceAdapterAssociation;
import javax.security.auth.Subject;

/**
 *
 * @author mscott
 */
public class WeaverEISManagedConnectionFactory implements ManagedConnectionFactory, ResourceAdapterAssociation {
    ResourceAdapter adapter;
    PrintWriter writer;
    /** Creates a new instance of WeaverEISManagedConnectionFactory */
    public WeaverEISManagedConnectionFactory() {
    }

    public Object createConnectionFactory(ConnectionManager connectionManager) throws ResourceException {
        return new WeaverEISConnectionFactory(connectionManager);
    }

    public Object createConnectionFactory() throws ResourceException {
        return new WeaverEISConnectionFactory();
    }

    public ManagedConnection createManagedConnection(Subject subject, ConnectionRequestInfo connectionRequestInfo) throws ResourceException {

    }

    public ManagedConnection matchManagedConnections(Set set, Subject subject, ConnectionRequestInfo connectionRequestInfo) throws ResourceException {
    
    }

    public void setLogWriter(PrintWriter printWriter) throws ResourceException {
        writer = printWriter;
    }

    public PrintWriter getLogWriter() throws ResourceException {
        return writer;
    }

    public ResourceAdapter getResourceAdapter() {
        return adapter;
    }

    public void setResourceAdapter(ResourceAdapter resourceAdapter) throws ResourceException {
        adapter = resourceAdapter;
    }
    
}
