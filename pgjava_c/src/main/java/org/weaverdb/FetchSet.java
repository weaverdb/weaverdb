/*-------------------------------------------------------------------------
 *
 *
 * Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 *
 * All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 *
 *-------------------------------------------------------------------------
 */
/*
 */
package org.weaverdb;

import org.weaverdb.FetchSet.Row;
import java.util.AbstractList;
import java.util.Iterator;
import java.util.Spliterator;
import java.util.function.Consumer;
import java.util.stream.Stream;
import java.util.stream.StreamSupport;

/**
 * Helper to provide a secondary fluent, streaming interface to the underlying DBReference.
 * This can be used to interact with the DBReference if preferred.
 */
public class FetchSet implements Iterable<Output[]>, Spliterator<Output[]> {
    
    private final Statement stmt;
    private final static int MAX_ATTRIBUTES = 20;

    FetchSet(Statement stmt) throws ExecutionException {
        this.stmt = stmt;
        long count = this.stmt.execute();
        if (count == 0) {
            // probable select statement
            if (stmt.outputs().isEmpty()) {
                for (int x=1;x<=MAX_ATTRIBUTES;x++) {
                    stmt.linkOutput(x, Object.class);
                }
            }
        } else {
            // not a select statement.  close
            stmt.close();// no statement reuse
        }
    }
    /**
     * Spliterator interface.
     * @param action consumer if the output rows
     * @return true if the spliterator was advanced
     */
    @Override
    public boolean tryAdvance(Consumer<? super Output[]> action) {
        try {
            boolean advanced = stmt.fetch();
            if (advanced) {
                action.accept(stmt.outputs().stream()
                        .filter(bo->bo.getName() != null)
                        .sorted((a, b)->Integer.compare(a.getIndex(), b.getIndex()))
                        .toArray(Output[]::new));
            } else {
                stmt.close();
            }
            return advanced;
        } catch (ExecutionException ee) {
            throw new RuntimeException(ee);
        }
    }
    /**
     * FetchSet will not split for a spliterator.
     * @return 
     */
    @Override
    public Spliterator<Output[]> trySplit() {
        return null;
    }
    /**
     * No size estimate available. Returns Long.MAX_VALUE
     * @return 
     */
    @Override
    public long estimateSize() {
        return Long.MAX_VALUE;
    }
    /**
     * Spliterator characteristics.
     * @return 
     */
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
                        .toArray(Output[]::new);
            }            
        };
    }
    /**
     * Stream the results of a statement.  If no outputs have been linked, A set of
     * default output links will be created.  
     * @param stmt Statement with results to be streamed.  no execute or fetches should have been 
     * executed on the statement.  Stream close will not close the statement.  It needs to be closed
     * separately.
     * @return Stream of rows
     * @throws ExecutionException 
     */
    public static Stream<Row> stream(Statement stmt) throws ExecutionException {
        Stream<Row> rows = StreamSupport.stream(new FetchSet(stmt), false).map(Row::new);
        return rows;
    }
    /**
     * FetchSet builder for fluent creation of a FetchSet.
     * @param conn
     * @return @see Builder
     */
    public static FetchSet.Builder builder(DBReference conn) {
        return new Builder(conn);
    }
    /**
     * A statement builder to fluently create a statement that will stream results.  
     * A close on the Stream will close the statement.
     */
    public static class Builder {
        private final DBReference connection;
        
        Builder(DBReference connection) {
            this.connection = connection;
        }
        /**
         * Parse a sql statement for execution.
         * @param stmt 
         * @return
         * @throws ExecutionException 
         */
        public StatementBuilder parse(String stmt) throws ExecutionException {
            return new StatementBuilder(connection.statement(stmt));
        }
    }
    /**
     * Statement builder to add linked input and output variables.
     */
    public static class StatementBuilder {
        private final Statement stmt;
        private ExecutionException ee;

        StatementBuilder(Statement stmt) throws ExecutionException {
                this.stmt = stmt;
        }
        /**
         * Indexed output in the parsed statement. 
         * @param <T> Type of the output variable.  Conversion is automatic if possible
         * @param pos 1 based position of the output variable to link (first listed is position 1)
         * @param type Class of the output type desired
         * @return StatementBuilder to continue building
         */
        public <T> StatementBuilder output(int pos, Class<T> type) {
            try {
                stmt.linkOutput(pos, type);
            } catch (ExecutionException ee) {
                this.ee = ee;
            }
            return this;
        }
        /**
         * Link an output transformer to convert a binary channel to a desired output type.
         * @param <T> Type of the output variable
         * @param pos position in the select statement of the binary target
         * @param convert conversion function converting a @see java.nio.channels.ReadableChannel to the output object.
         * @return StatementBuilder to continue building
         */
        public <T> StatementBuilder output(int pos, Output.Channel<T> convert) {
            try {
                stmt.linkOutputChannel(pos, convert);
            } catch (ExecutionException ee) {
                this.ee = ee;
            }
            return this;
        }
        /**
         * Link a named input variable in the parsed statement.  Named variables start 
         * with a '$' for example select id from foo where bar = $bar
         * @param <T> Type of the input variable.  Automatic conversion occurs if possible
         * @param name name of the variable without the indicator ($)
         * @param value input variable for the parsed statement
         * @return StatementBuilder to continue building
         */
        public <T> StatementBuilder input(String name, T value) {
            try {
                stmt.linkInput(name, (Class<T>)value.getClass()).set(value);
            } catch (ExecutionException ee) {
                this.ee = ee;
            }
            return this;
        }
        /**
         * Link a binary transformer to write an input object to a @see java.nio.channels.WritableChannel
         * @param <T> Type of the input object
         * @param name name of the binary input variable
         * @param convert conversion function that writes the object to a @see java.nio.channels.WritableChannel
         * @param value input object
         * @return StatementBuilder to continue building
         */
        public <T> StatementBuilder input(String name, Input.Channel<T> convert, T value) {
            try {
                stmt.linkInputChannel(name, convert).set(value);
            } catch (ExecutionException ee) {
                this.ee = ee;
            }
            return this;
        }
        /**
         * Execute the prepared statement and Stream the output.
         * @return Stream of rows which must be closed on completion.  Consider using try-with-resources
         * @throws ExecutionException 
         */        
        public Stream<Row> execute() throws ExecutionException {
            if (ee != null) {
                throw ee;
            }
            return StreamSupport.stream(new FetchSet(stmt), false)
                .map(Row::new)
                .onClose(stmt::close);
        }
    }
    /**
     * A row of outputs from the PreparedStatement.
     * 
     */
    public static class Row extends AbstractList<Column> {
        
        private final Output[] columns;

        public Row(Output[] columns) {
            this.columns = columns;
        }
        /**
         * One output of the Row.  1 based indexing
         * @param index position of the output
         * @return 
         */
        @Override
        public Column get(int index) {
            return new Column(columns[index]);
        }
        /**
         * number of output columns in the row.
         * @return total size
         */
        @Override
        public int size() {
            return columns.length;
        }
    }
    /**
     * One output of a row.
     * 
     */
    public static class Column {
        private final Output<?> output;

        public Column(Output<?> output) {
            this.output = output;
        }
        /**
         * name of the column as defined by the prepared statement.
         * @return name
         */
        public String getName() {
            return output.getName();
        }
        /**
         * current value of the Output.
         * @return 
         */
        public Object get() {
            try {
                return output.get();
            } catch (ExecutionException ee) {
                return null;
            }
        }
        /**
         * True if a valid column for the fetch.
         * @return 
         */
        public boolean isValid() {
            return output.getName() != null;
        }
        
        @Override
        public String toString() {
            return getName() + "=" + get();
        }
    }
}
