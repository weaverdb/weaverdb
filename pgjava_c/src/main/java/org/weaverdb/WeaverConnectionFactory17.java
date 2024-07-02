
package org.weaverdb;

public class WeaverConnectionFactory17 implements ConnectionFactory {
    
    @Override
    public Connection connect(String db) {
        return BaseWeaverConnection.connectAnonymously(db, new StreamingTransformer17());
    }
    
    @Override
    public Connection connectUser(String username, String password, String database) {
        return BaseWeaverConnection.connectUser(username, password, database, new StreamingTransformer17());
    }

    @Override
    public Runtime.Version builtFor() {
        return Runtime.Version.parse("17");
    }
    
    
}
