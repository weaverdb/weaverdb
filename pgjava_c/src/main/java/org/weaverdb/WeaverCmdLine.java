/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/Classes/Class.java to edit this template
 */
package org.weaverdb;

/**
 *
 * @author myronscott
 */
public class WeaverCmdLine {
    public static native int cmd(String args[]);
    
    public static void main(String args[]) {
        cmd(args);
    }
}
