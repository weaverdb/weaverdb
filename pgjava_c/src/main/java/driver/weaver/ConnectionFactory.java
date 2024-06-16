/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/Classes/Class.java to edit this template
 */
package driver.weaver;

/**
 *
 * @author myronscott
 */
public interface ConnectionFactory {
    Connection connectAnonymousy(String db);
    
    Connection connectUser(String username, String password, String database);
}
