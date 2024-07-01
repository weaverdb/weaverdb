package org.weaverdb;

/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/Classes/Class.java to edit this template
 */

/**
 *
 * @author myronscott
 */
public class WeaverConnectionFactory21 implements ConnectionFactory {

    @Override
    public Connection connectAnonymousy(String db) {
        return BaseWeaverConnection.connectAnonymously(db, new StreamingTransformer21());
    }

    @Override
    public Connection connectUser(String username, String password, String database) {
        return BaseWeaverConnection.connectUser(username, password, database, new StreamingTransformer21());
    }

    @Override
    public Runtime.Version builtFor() {
        return Runtime.Version.parse("21");
    }
    
}
