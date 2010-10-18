/*
 * WeaverEISConnection.java
 *
 * Created on October 21, 2006, 5:02 PM
 *
 * To change this template, choose Tools | Template Manager
 * and open the template in the editor.
 */

package driver.weaver.connector;

import javax.resource.ResourceException;
import javax.resource.cci.Connection;
import javax.resource.cci.ConnectionMetaData;
import javax.resource.cci.Interaction;
import javax.resource.cci.LocalTransaction;
import javax.resource.cci.ResultSetInfo;
import javax.resource.spi.ResourceAdapter;
import javax.resource.spi.ResourceAdapterAssociation;

/**
 *
 * @author mscott
 */
public class WeaverEISConnection implements Connection, ResourceAdapterAssociation {

    /** Creates a new instance of WeaverEISConnection */
    public WeaverEISConnection() {
    }

    public Interaction createInteraction() throws ResourceException {
    }

    public LocalTransaction getLocalTransaction() throws ResourceException {
    }

    public ConnectionMetaData getMetaData() throws ResourceException {
    }

    public ResultSetInfo getResultSetInfo() throws ResourceException {
    }

    public void close() throws ResourceException {
    }

    public ResourceAdapter getResourceAdapter() {
    }

    public void setResourceAdapter(ResourceAdapter resourceAdapter) throws ResourceException {
    }

}
