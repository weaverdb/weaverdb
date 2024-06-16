/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/Classes/Class.java to edit this template
 */
package org.weaver;

/**
 *
 * @author myronscott
 */
public class WeaverConnectionFactory implements ConnectionFactory {
    @Override
    public Connection connectAnonymousy(String db) {
        return BaseWeaverConnection.connectAnonymously(db);
    }
    
    @Override
    public Connection connectUser(String username, String password, String database) {
        return BaseWeaverConnection.connectUser(username, password, database);
    }
}
