
package org.weaverdb;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandleInfo;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/**
 *
 */
public class FunctionInstaller {
    private final Connection connection;
    private static final Pattern clazz = Pattern.compile("L(.*);");
    

    public FunctionInstaller(Connection connection) {
        this.connection = connection;
    }
    
    public void installFunction(String alias, MethodHandle definition) throws ExecutionException {
        String stmt = createFunctionStatement(alias, definition);
        connection.execute(stmt);
    }
    
    private static String createFunctionStatement(String name, MethodHandle definition) {
        MethodHandleInfo info = MethodHandles.lookup().revealDirect(definition);
        StringBuilder builder = new StringBuilder();
        Matcher cm = clazz.matcher(info.getDeclaringClass().descriptorString());
        String fullname;
        if (cm.find()) {
            fullname = cm.group(1) + '.' + info.getName();
        } else {
            throw new RuntimeException();
        }
        if (name == null) {
            name = fullname;
        }
        MethodType mt = info.getMethodType();
        if (name != null && info.getReferenceKind() == MethodHandleInfo.REF_invokeVirtual) {
            mt = mt.insertParameterTypes(0, info.getDeclaringClass());        
        }
        builder.append("create function '");
        builder.append(name);
        builder.append('\'');
        Stream<Class<?>> c = mt.parameterList().stream();
        String args = c.map(FunctionInstaller::convertClass).collect(Collectors.joining(",", "(", ")"));
        builder.append(" ");
        builder.append(args);
        builder.append(" returns ");
        builder.append(convertClass(info.getMethodType().returnType()));
        builder.append(" as ");
        builder.append('\'');
        builder.append(fullname);
        builder.append('\'');
        builder.append(',');
        builder.append('\'');
        builder.append(info.getMethodType().descriptorString());
        builder.append('\'');
        builder.append(" language 'java'");
       
        return builder.toString();
    }

    private static String convertClass(Class<?> c) {
        if (c.equals(Integer.class) || c.equals(int.class)) {
            return "int4";
        } else if (c.equals(String.class)) {
            return "varchar";
        } else if (c.equals(Double.class) || c.equals(double.class)) {
            return "float8";
        } else if (c.equals(Long.class) || c.equals(long.class)) {
            return "int8";
        } else if (c.equals(Boolean.class) || c.equals(boolean.class)) {
            return "bool";
        }
        return "java";
    }
}
