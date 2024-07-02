
package org.weaverdb;

public interface ConnectionFactory {

    Connection connect(String database);
    
    Connection connectUser(String username, String password, String database);
    
    Runtime.Version builtFor();
}
