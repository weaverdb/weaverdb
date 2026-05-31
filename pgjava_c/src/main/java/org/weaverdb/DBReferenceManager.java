

package org.weaverdb;

import java.util.Iterator;
import java.util.ServiceLoader;

/**
 *
 * @author myronscott
 */
public class DBReferenceManager {
    private final static DBReferenceFactory LOADER = loadConnectionFactory();
   /**
     * Connect and retrieve a reference to a database in the current Weaver instance.  
     * Weaver must be loaded and initialized before this call will work.  
     * @see org.weaverdb.WeaverInitializer#initialize
     * @param database name of the database to connect to
     * @return 
     */
    public static DBReference connect(String database) {
        return LOADER.connect(database);
    }
    
    public static DBReference connect(String name, String password, String database) {
        return LOADER.connectUser(name, password, database);
    }
    
    private static DBReferenceFactory loadConnectionFactory() {
        try {
            Runtime.Version runningVersion = Runtime.version();

            ServiceLoader<DBReferenceFactory> check = ServiceLoader.load(DBReferenceFactory.class);
            Iterator<DBReferenceFactory> versions = check.iterator();
            DBReferenceFactory winner = null;
            while (versions.hasNext()) {
                DBReferenceFactory candidate = versions.next();
                if (Runtime.Version.parse(candidate.builtFor()).compareTo(runningVersion) <= 0) {
                    if (winner == null || winner.builtFor().compareTo(candidate.builtFor()) < 0) {
                        winner = candidate;
                    }
                }
            }
            return winner;
        } catch (Throwable t) {
            ServiceLoader<DBReferenceFactory> factories = ServiceLoader.load(DBReferenceFactory.class);
            for (DBReferenceFactory f : factories) {
                return f;
            }
        }
        return null;
    }
    
    public static boolean hasLiveConnections() {
        return LOADER.hasLiveConnections();
    }
}
