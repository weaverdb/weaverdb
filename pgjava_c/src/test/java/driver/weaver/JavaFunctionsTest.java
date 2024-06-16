/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/UnitTests/JUnit5TestClass.java to edit this template
 */
package driver.weaver;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandleInfo;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.BeforeAll;

/**
 *
 * @author myronscott
 */
public class JavaFunctionsTest {
    
    public JavaFunctionsTest() {
    }
    
    @BeforeAll
    public static void setUpClass() {
    }
    
    @AfterAll
    public static void tearDownClass() {
    }
    
    @BeforeEach
    public void setUp() {
    }
    
    @AfterEach
    public void tearDown() {
    }

    
    @org.junit.jupiter.api.Test
    public void testAutoMethod() throws Exception {
//        Field f = java.lang.reflect.Method.class.getDeclaredField("signature");
//        f.setAccessible(true);
        MethodHandle handle = MethodHandles.lookup().findVirtual(Integer.class, "toString", MethodType.methodType(String.class));
        System.out.println(handle.toString());
        System.out.println(handle.type().toString());
        System.out.println(handle.type().toMethodDescriptorString());
        System.out.println(handle.type().descriptorString());
        System.out.println(handle.describeConstable().get().toString());
        MethodHandleInfo info = MethodHandles.lookup().revealDirect(handle);
        System.out.println(info.getMethodType().toMethodDescriptorString());
        System.out.println(info.getName());
        System.out.println(info.getDeclaringClass().descriptorString());
        System.out.println(info.getMethodType().descriptorString());
        System.out.println(info.getReferenceKind());
        System.out.println(MethodHandleInfo.referenceKindToString(info.getReferenceKind()));
    }
}
