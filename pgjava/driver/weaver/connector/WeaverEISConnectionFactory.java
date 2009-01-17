/*
 * WeaverEISConnection.java
 *
 * Created on October 20, 2006, 3:08 PM
 *
 * To change this template, choose Tools | Template Manager
 * and open the template in the editor.
 */

package driver.weaver.connector;

import java.io.PrintWriter;
import java.util.Set;
import javax.naming.NamingException;
import javax.naming.Reference;
import javax.resource.ResourceException;
import javax.resource.cci.Connection;
import javax.resource.cci.ConnectionFactory;
import javax.resource.cci.ConnectionSpec;
import javax.resource.cci.RecordFactory;
import javax.resource.cci.ResourceAdapterMetaData;
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
public class WeaverEISConnectionFactory implements ConnectionFactory, ResourceAdapterAssociation {
    ConnectionManager manager = null;
    /** Creates a new instance of WeaverEISConnection */
    public WeaverEISConnectionFactory() {
    }
    
    public WeaverEISConnectionFactory(ConnectionManager mng) {
        manager = mng;
    }

    public Connection getConnection() throws ResourceException {
    }

    public Connection getConnection(ConnectionSpec connectionSpec) throws ResourceException {
    }

    public RecordFactory getRecordFactory() throws ResourceException {
    }

    public ResourceAdapterMetaData getMetaData() throws ResourceException {
    }

    public void setReference(Reference reference) {
    }

    public Reference getReference() throws NamingException {
    }

    public ResourceAdapter getResourceAdapter() {
    }

    public void setResourceAdapter(ResourceAdapter resourceAdapter) throws ResourceException {
    }


}
