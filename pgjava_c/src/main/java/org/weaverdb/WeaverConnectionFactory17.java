/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/Classes/Class.java to edit this template
 */
package org.weaverdb;

import java.io.IOException;
import java.util.jar.Attributes;
import java.util.jar.Manifest;

/**
 *
 * @author myronscott
 */
public class WeaverConnectionFactory17 implements ConnectionFactory {
    StreamingTransformer transformer = new StreamingTransformer17();
    
    @Override
    public Connection connectAnonymousy(String db) {
        return BaseWeaverConnection.connectAnonymously(db, transformer);
    }
    
    @Override
    public Connection connectUser(String username, String password, String database) {
        return BaseWeaverConnection.connectUser(username, password, database, transformer);
    }

    @Override
    public Runtime.Version builtFor() {
        return Runtime.Version.parse("17");
    }
    
    
}
