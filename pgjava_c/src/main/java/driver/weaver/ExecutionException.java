


package driver.weaver;


public class ExecutionException extends Exception {
    
    public ExecutionException(String s) {
        super(s);
    }
    
    public ExecutionException(Throwable  s) {
        super(s.getMessage());
        this.initCause(s);
    }
}