
package org.weaverdb;

public interface ConnectionFactory {

    Connection connectAnonymousy(String db);
    
    Connection connectUser(String username, String password, String database);
    
    Runtime.Version builtFor();
}
