
package driver.weaver;

import driver.weaver.BaseWeaverConnection.Statement;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandleInfo;
import java.lang.invoke.MethodHandles;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/**
 *
 */
public class FunctionInstaller {
    private final BaseWeaverConnection connection;
    private static final Pattern clazz = Pattern.compile("L(.*);");
    

    public FunctionInstaller(BaseWeaverConnection connection) {
        this.connection = connection;
    }
    
    public static void main(String[] args) throws Exception {
        MethodHandle handle = MethodHandles.lookup().unreflect(Object.class.getMethod("toString"));
        System.out.println(installInstanceFunction(handle));
        handle = MethodHandles.lookup().unreflect(Integer.class.getMethod("toString", int.class, int.class));
        System.out.println(installStaticFunction("radix", handle));
    }
    
    public static String installStaticFunction(String name, MethodHandle definition) throws ExecutionException {
        MethodHandleInfo info = MethodHandles.lookup().revealDirect(definition);
        StringBuilder builder = new StringBuilder();
        builder.append("create function ");
        builder.append(name);
        Stream<Class<?>> c = info.getMethodType().parameterList().stream();
        String args = c.map(FunctionInstaller::convertClass).collect(Collectors.joining(",", "(", ")"));
        builder.append(" ");
        builder.append(args);
        builder.append(" returns ");
        builder.append(convertClass(info.getMethodType().returnType()));
        builder.append(" as ");
        builder.append('\'');
        Matcher cm = clazz.matcher(info.getDeclaringClass().descriptorString());
        if (cm.find()) {
            builder.append(cm.group(1));
            builder.append(".");
            builder.append(info.getName());
        } else {
            throw new RuntimeException();
        }
        builder.append('\'');
        builder.append(',');
        builder.append('\'');
        builder.append(info.getMethodType().descriptorString());
        builder.append('\'');
        builder.append(" language 'java'");
        return builder.toString();
    }
    
    public static String installInstanceFunction(MethodHandle definition) throws ExecutionException {
        MethodHandleInfo info = MethodHandles.lookup().revealDirect(definition);
        StringBuilder builder = new StringBuilder();
        builder.append("create function ");
        Matcher cm = clazz.matcher(info.getDeclaringClass().descriptorString());
        if (cm.find()) {
            builder.append(cm.group(1));
            builder.append(".");
            builder.append(info.getName());
        } else {
            throw new RuntimeException();
        }
        Stream<Class<?>> c = info.getMethodType().parameterList().stream();
        String args = c.map(FunctionInstaller::convertClass).collect(Collectors.joining(",", "(", ")"));
        builder.append(" ");
        builder.append(args);
        builder.append(" returns ");
        builder.append(convertClass(info.getMethodType().returnType()));
        builder.append(" as ");
        builder.append('\'');
        builder.append(info.getName());
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
        }
        return "";
    }
}
