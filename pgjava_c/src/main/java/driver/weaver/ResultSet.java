/*
 */
package driver.weaver;

import driver.weaver.BaseWeaverConnection.Statement;
import driver.weaver.ResultSet.Row;
import java.util.Arrays;
import java.util.Collection;
import java.util.Iterator;
import java.util.Spliterator;
import java.util.function.Consumer;
import java.util.stream.Stream;
import java.util.stream.StreamSupport;

/**
 *
 */
public class ResultSet implements Iterable<Output[]>, Spliterator<Output[]> {
    
    private final Statement stmt;
    private final static int MAX_ATTRIBUTES = 20;

    ResultSet(Statement stmt) {
        this.stmt = stmt;
    }

    @Override
    public boolean tryAdvance(Consumer<? super Output[]> action) {
        try {
            boolean advanced = stmt.fetch();
            if (advanced) {
                action.accept(stmt.outputs().stream()
                        .filter(bo->bo.getName() != null)
                        .sorted((a, b)->Integer.compare(a.getIndex(), b.getIndex()))
                        .map(Output::new).toArray(Output[]::new));
            }
            return advanced;
        } catch (ExecutionException ee) {
            throw new RuntimeException(ee);
        }
    }

    @Override
    public Spliterator<Output[]> trySplit() {
        return null;
    }

    @Override
    public long estimateSize() {
        return Long.MAX_VALUE;
    }

    @Override
    public int characteristics() {
        return Spliterator.IMMUTABLE | Spliterator.NONNULL | Spliterator.ORDERED;
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
    
    public static Stream<Row> stream(BaseWeaverConnection conn, String stmt) throws ExecutionException {
        Statement s = conn.statement(stmt);
        for (int x=1;x<=MAX_ATTRIBUTES;x++) {
            s.linkOutput(x, Object.class);
        }
        s.execute();
        Stream<Row> rows = StreamSupport.stream(new ResultSet(s), false).map(oa->new Row(Arrays.stream(oa).map(Column::new).toList()));
        rows.onClose(s::close);
        return rows;
    }
    
    public static Stream<Row> stream(Statement stmt) throws ExecutionException {
        Stream<Row> rows = StreamSupport.stream(new ResultSet(stmt), false).map(oa->new Row(Arrays.stream(oa).map(Column::new).toList()));
        return rows;
    }
    
    public static class Row implements Iterable<Column> {
        
        private final Collection<Column> columns;

        public Row(Collection<Column> columns) {
            this.columns = columns;
        }
        
        public Stream<Column> stream() {
            return columns.stream();
        }

        @Override
        public Iterator<Column> iterator() {
            return columns.iterator();
        }
    }
    
    public static class Column {
        private final Output<?> output;

        public Column(Output<?> output) {
            this.output = output;
        }

        public String getName() {
            return output.getName();
        }

        public Object get() {
            try {
                return output.get();
            } catch (ExecutionException ee) {
                return null;
            }
        }
        
        public boolean isValid() {
            return output.getName() != null;
        }
        
        @Override
        public String toString() {
            return getName() + "=" + get();
        }
    }
}
