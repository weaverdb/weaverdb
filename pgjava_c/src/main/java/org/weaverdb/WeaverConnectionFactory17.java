
package org.weaverdb;

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
