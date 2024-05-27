/*
 */
package driver.weaver;

import driver.weaver.BaseWeaverConnection.Statement;
import java.util.Iterator;
import java.util.stream.Stream;
import java.util.stream.StreamSupport;

/**
 *
 */
public class ResultSet implements Iterable<Output[]> {
    
    private final Statement stmt;
    private final static int MAX_ATTRIBUTES = 20;

    public ResultSet(Statement stmt) {
        this.stmt = stmt;
    }

    @Override
    public Iterator<Output[]> iterator() {
        return new Iterator<>() {
            @Override
            public boolean hasNext() {
                try {
                    return stmt.fetch();
                } catch (ExecutionException ee) {
                    throw new RuntimeException(ee);
                }
            }

            @Override
            public Output[] next() {
                return stmt.outputs().stream()
                        .filter(bo->bo.getName() != null)
                        .sorted((a, b)->Integer.compare(a.getIndex(), b.getIndex()))
                        .map(Output::new).toArray(Output[]::new);
            }            
        };
    }
    
    public static Stream<Output[]> stream(BaseWeaverConnection con, String stmt) throws ExecutionException {
        Statement s = con.statement(stmt);
        Stream<Output[]> results = stream(s);
        results.onClose(s::close);
        return results;
    }

    public static Stream<Output[]> stream(Statement stmt) throws ExecutionException {
        if (stmt.outputs().isEmpty()) {
            for (int x=1;x<=MAX_ATTRIBUTES;x++) {
                stmt.linkOutput(x, Object.class);
            }
        }
        return StreamSupport.stream(new ResultSet(stmt).spliterator(), false);
    }
}
