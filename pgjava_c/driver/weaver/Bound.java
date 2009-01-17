/*
 * BindVar.java
 *
 * Created on February 27, 2003, 8:35 AM
 */

package driver.weaver;

/**
 *
 * @author  mscott
 */
public class Bound {
    enum Types {
        String(2,"Ljava/lang/String;"),
        Double(3,"D"),
        Integer(1,"I"),
        Binary(6,"[B"),
        BLOB(7,"[B"),
        Character(4,"C"),
        Boolean(5,"Z"),
        Date(12,"[B"),
        Long(13,"[B"),
        Function(20,"[B"),
        Slot(30,"[B"),
        Java(40,"[B"),
        Text(41,"[B"),
        Stream(42,"<assigned>"),
        Direct(43,"<direct>"),
        Null(0,"");
                
        private int id;
        private String signature;
        
        Types(int id, String sig) {
            this.id = id;
            this.signature = sig;
        }
        
        public int getId() {
            return id;
        }
        
        public String getSignature() {
            return signature;
        }
    }
}
