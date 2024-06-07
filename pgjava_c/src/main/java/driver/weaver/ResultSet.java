/*
 */
package driver.weaver;

import driver.weaver.BaseWeaverConnection.Statement;
import driver.weaver.ResultSet.Row;
import java.util.AbstractList;
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

    ResultSet(Statement stmt) throws ExecutionException {
        this.stmt = stmt;
        if (stmt.outputs().isEmpty()) {
            for (int x=1;x<=MAX_ATTRIBUTES;x++) {
                stmt.linkOutput(x, Object.class);
            }
        }
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
    
    public static ResultSet.Builder builder(BaseWeaverConnection conn) {
        return new Builder(conn);
    }
    
    public static Stream<Row> stream(Statement stmt) throws ExecutionException {
        Stream<Row> rows = StreamSupport.stream(new ResultSet(stmt), false).map(Row::new);
        return rows;
    }
    
    public static class Builder {
        private final BaseWeaverConnection connection;
        
        Builder(BaseWeaverConnection connection) {
            this.connection = connection;
        }
        
        public StatementBuilder parse(String stmt) throws ExecutionException {
            return new StatementBuilder(connection.statement(stmt));
        }
    }
    
    public static class StatementBuilder {
        private final Statement stmt;
        private ExecutionException ee;

        public StatementBuilder(Statement stmt) throws ExecutionException {
                this.stmt = stmt;
        }

        public <T> StatementBuilder output(int pos, Class<T> type) {
            try {
                stmt.linkOutput(pos, type);
            } catch (ExecutionException ee) {
                this.ee = ee;
            }
            return this;
        }
        
        public <T> StatementBuilder output(int pos, Output.Channel<T> convert) {
            try {
                stmt.linkOutputChannel(pos, convert);
            } catch (ExecutionException ee) {
                this.ee = ee;
            }
            return this;
        }
        
        public <T> StatementBuilder input(String name, T value) {
            try {
                stmt.linkInput(name, (Class<T>)value.getClass()).set(value);
            } catch (ExecutionException ee) {
                this.ee = ee;
            }
            return this;
        }
        
        public <T> StatementBuilder input(String name, Input.Channel<T> convert) {
            try {
                stmt.linkInputChannel(name, convert);
            } catch (ExecutionException ee) {
                this.ee = ee;
            }
            return this;
        }
        
        public Stream<Row> execute() throws ExecutionException {
            if (ee != null) {
                throw ee;
            }
            return StreamSupport.stream(new ResultSet(stmt), false)
                .map(Row::new)
                .onClose(stmt::close);
        }
    }
    
    public static class Row extends AbstractList<Column> {
        
        private final Output[] columns;

        public Row(Output[] columns) {
            this.columns = columns;
        }

        @Override
        public Column get(int index) {
            return new Column(columns[index]);
        }

        @Override
        public int size() {
            return columns.length;
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
