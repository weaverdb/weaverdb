/*
 * JavaConverter.java
 *
 * Created on August 23, 2004, 3:20 PM
 */

package driver.weaver;

import java.io.*;
import java.lang.reflect.*;
/**
 *
 * @author  mscott
 */
public class JavaConverter extends ThreadLocal<JavaConverter> implements ObjectStreamConstants {
    
    static JavaConverter loc = new JavaConverter();
    
    ByteWellInputStream in;
    ObjectInputStream oin;
    ByteWellOutputStream out;
    ObjectOutputStream oout;
    
    /** Creates a new instance of JavaConverter */
    public JavaConverter() {
        try {
            out = new ByteWellOutputStream();
            oout = new ObjectOutputStream(out);
            oout.flush();
            /* prime the input stream with the data from the input stream  */
            in = new ByteWellInputStream();
            byte[] well = out.getByteWell();
            in.setByteWell(well);
            oin = new ObjectInputStream(in);
        } catch ( IOException ioe ) {
            ioe.printStackTrace();
        }
    }
    
    public JavaConverter initialValue() {
        return new JavaConverter();
    }
    
    public Object out(byte[] input) {
        try {
            in.setByteWell(input);
            return oin.readObject();
        } catch ( IOException ioe ) {
            ioe.printStackTrace();
            return null;
        } catch ( ClassNotFoundException ce ) {
            ce.printStackTrace();
            return null;
        } catch ( Throwable t ) {
            t.printStackTrace();
            return null;
        }
    }
    
    public static Object java_out(byte[] input) {
        return loc.get().out(input);
    }
    
    public byte[] in(Object input) {
        try {
            oout.reset();
            oout.writeObject(input);
            oout.flush();
            byte[] b = out.getByteWell();
            
            return b;
        } catch ( IOException ioe ) {
            return null;
        }
    }
    
    public static byte[] java_in(Object input) {
        return loc.get().in(input);
    }
    
    public static byte[] java_text_in(String input) {
        int br = input.indexOf('(');
        String name = input.substring(0, br ).trim();
        String arg = input.substring(br).trim();
        
        arg = arg.substring(1,arg.length()-2);
        try {
            Class c = Class.forName(name);
            
            Constructor cont = c.getConstructor(String.class);
            Object obj = cont.newInstance(arg);
            return java_in(obj);
        } catch ( Exception exp ) {
            exp.printStackTrace();
            return null;
        }
    }
    
    public static String java_text_out(byte[] output) {
        Object obj = java_out(output);
        Class name = obj.getClass();
        StringBuilder build = new StringBuilder();
        
        build.append(name.getName());
        build.append('(');
        build.append(obj.toString());
        build.append(')');
        
        return build.toString();
    }
    
    
    public static boolean java_equals(byte[] obj1,byte[] obj2) {
        JavaConverter jc = loc.get();
        
        Object o1 = jc.out(obj1);
        Object o2 = jc.out(obj2);
        
        return o1.equals(o2);
    }
    
    public static int java_compare(byte[] obj1,byte[] obj2) {
        JavaConverter jc = loc.get();
        
        Comparable o1 = (Comparable)jc.out(obj1);
        Comparable o2 = (Comparable)jc.out(obj2);
        
        return o1.compareTo(o2);
    }
    
    /*
    public Object custom_out(byte[] input) {
        in.setByteWell(input);
        try {
            return dis.readObject();
        } catch ( Exception cl ) {
            throw new RuntimeException(cl);
        }
    }
     
    public byte[] custom_in(Object input) throws IOException {
        try {
            dos.writeObject(input);
            dos.flush();
            return out.getByteWell();
        } catch ( IOException ioe ) {
            throw new RuntimeException(ioe);
        }
    }
     
    java.util.HashMap fieldmap = new java.util.HashMap();
     
    public Field[] getFields(Class target) {
        Field[] map = (Field[])fieldmap.get(target);
        if ( map == null ) {
            target.getDeclaredFields();
            fieldmap.put(target, map);
        }
        return map;
    }
     
    public static void rawFieldDump(Object target) {
        try {
            if ( target == null ) return;
            Class os = target.getClass();
            while (os != Object.class) {
                Field[] items = os.getDeclaredFields();
     
                for (int x=0;x<items.length;x++) {
     
                    int mods = items[x].getModifiers();
                    if (!Modifier.isTransient(mods) && !Modifier.isFinal(mods) && !Modifier.isStatic(mods) ) {
                        items[x].setAccessible(true);
                        Class t = items[x].getType();
                        System.out.print(items[x].getName());
                        System.out.print(" ");
                        if ( items[x].getType().isPrimitive() ) {
                            if ( t == Character.class ) {
                                System.out.print(items[x].getChar(target));
                            } else if ( t == Integer.class ) {
                                System.out.print(items[x].getInt(target));
                            } else if ( t == Double.class ) {
                                System.out.print(items[x].getDouble(target));
                            } else if ( t == Byte.class ) {
                                System.out.print(items[x].getByte(target));
                            } else if ( t == Long.class ) {
                                System.out.print(items[x].getLong(target));
                            } else if ( t == Boolean.class ) {
                                System.out.print(items[x].getBoolean(target));
                            }
                        } else if ( t == String.class ) {
                            System.out.print(items[x].get(target));
                        } else {
                            rawFieldDump(items[x].get(target));
                        }
                    }
                }
                os = os.getSuperclass();
            }
        } catch ( Exception exp ) {
            exp.printStackTrace();
        }
    }
     
    public static void dumpFields(Class os) {
        ObjectStreamClass oc = ObjectStreamClass.lookup(os);
        System.out.println(Serializable.class.isAssignableFrom(os));
        System.out.println(Externalizable.class.isAssignableFrom(os));
     
        System.out.println(oc);
     
        ObjectStreamField[] of = oc.getFields();
        System.out.println(of.length);
        for (int x=0;x<of.length;x++) {
            if ( of[x].isPrimitive() || (of[x].getType() == String.class) ) {
                StringBuilder b = new StringBuilder();
                b.append(of[x].getName() + " " + of[x].getTypeCode() +
                        " " + of[x].getTypeString() + " " + of[x].isUnshared() +
                        " " + of[x].getOffset() + " " + of[x].toString());
                System.out.println(b.toString());
            } else {
                System.out.println(of[x].getType());
                dumpFields(of[x].getType());
            }
        }
    }
     
    class ConverterInput extends DataInputStream implements ObjectInput {
        public ConverterInput(InputStream is) {
            super(is);
        }
     
        public Object readObject() throws ClassNotFoundException, IOException {
            String cla = readUTF();
            Class type = Class.forName(cla);
            if ( java.io.Externalizable.class.isAssignableFrom(type) ) {
                try {
                    java.io.Externalizable ex = (java.io.Externalizable)type.newInstance();
                    ex.readExternal(this);
                    return ex;
                } catch  ( Exception exp ) {
                    throw new IOException(exp.getMessage());
                }
            } else {
                try {
                    Object ex = (java.io.Serializable)type.newInstance();
                    readFields(type,ex);
                    return ex;
                } catch  ( Exception exp ) {
                    throw new IOException(exp.getMessage());
                }
            }
        }
     
        private boolean readFields(Class type,Object target) throws IOException, IllegalAccessException, InstantiationException {
            Field[] items = getFields(type);
            for (int x=0;x<items.length;x++) {
                int mods = items[x].getModifiers();
                if (!Modifier.isTransient(mods) && !Modifier.isFinal(mods) && !Modifier.isStatic(mods) ) {
                    Class t = items[x].getType();
                    byte check = readByte();
                    if ( check == 'N' ) continue;
     
                    if ( items[x].getType().isPrimitive() ) {
                        if ( t == Character.class ) {
                            items[x].setChar(target, readChar());
                        } else if ( t == Integer.class ) {
                            items[x].setInt(target, readInt());
                        } else if ( t == Double.class ) {
                            items[x].setDouble(target, readDouble());
                        } else if ( t == Byte.class ) {
                            items[x].setByte(target, readByte());
                        } else if ( t == Long.class ) {
                            items[x].setLong(target, readLong());
                        } else if ( t == Boolean.class ) {
                            items[x].setBoolean(target,readBoolean());
                        }
                    } else {
                        if ( t == String.class ) {
                            items[x].set(target,readUTF());
                        } else {
                            Object newtar = t.newInstance();
                            readFields(t, newtar);
                            items[x].set(target, newtar);
                        }
                }
            }
            Class sc = type.getSuperclass();
            if ( sc != Object.class ) {
                byte check = readByte();
                readFields(sc, target);
                return false;
            } else {
                return true;
            }
        }
        return true;
    }
     
    class ConverterOutput extends DataOutputStream implements ObjectOutput {
        public ConverterOutput(OutputStream is) {
            super(is);
        }
     
        public void writeObject(Object out) throws IOException {
            Class type = out.getClass();
            writeUTF(type.getName());
            if ( java.io.Externalizable.class.isAssignableFrom(type) ) {
                ((Externalizable)out).writeExternal(this);
            } else {
     
            }
        }
     
        private boolean writeFields(Class type,Object target) throws IOException, IllegalAccessException {
            Field[] items = getFields(type);
            for (int x=0;x<items.length;x++) {
                int mods = items[x].getModifiers();
                if (!Modifier.isTransient(mods) && !Modifier.isFinal(mods) && !Modifier.isStatic(mods) ) {
                    Class t = items[x].getType();
     
                    if ( items[x].getType().isPrimitive() ) {
                        writeByte('P');
                        if ( t == Character.class ) {
                            writeChar(items[x].getChar(target));
                        } else if ( t == Integer.class ) {
                            writeInt(items[x].getInt(target));
                        } else if ( t == Double.class ) {
                            writeDouble(items[x].getDouble(target));
                        } else if ( t == Byte.class ) {
                            writeByte(items[x].getByte(target));
                        } else if ( t == Long.class ) {
                            writeLong(items[x].getLong(target));
                        } else if ( t == Boolean.class ) {
                            writeBoolean(items[x].getBoolean(target));
                        }
                    } else {
                        Object s = items[x].get(target);
                        if ( t == null ) {
                           writeByte('N');
                        } else {
                            if ( t == String.class ) {
                                writeByte('S');
                                writeUTF(s.toString());
                            } else {
                                writeByte('O');
                                writeFields(t, s);
                            }
                        }
                    }
                }
            }
            Class sc = type.getSuperclass();
            if ( sc != Object.class ) {
                writeByte('U');
                writeFields(sc, target);
                return false;
            } else {
                return true;
            }
        }
    }
}
     **/
}
