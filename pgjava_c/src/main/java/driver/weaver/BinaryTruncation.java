


package driver.weaver;


public class BinaryTruncation extends Exception {

    public int maxcount = -1;
    
    public BinaryTruncation(String s) {
        super(s);
        maxcount = Integer.parseInt(s);
    }
    
    public BinaryTruncation(int max) {
        super(Integer.toString(max));
        maxcount = max;
    }
    
    public BinaryTruncation(Throwable  s) {
        super(s.getMessage());
        this.initCause(s);
    }
    
    public int getMaxFieldLength() {
        return maxcount;
    }
}