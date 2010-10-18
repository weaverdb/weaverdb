import java.io.IOException;
/*
 * BTPageScanner.java
 *
 * Created on April 19, 2007, 5:20 PM
 *
 * To change this template, choose Tools | Template Manager
 * and open the template in the editor.
 */

/**
 *
 * @author mscott
 */
public class BTPageScanner {
    static int pagesize = 8192;
    /** Creates a new instance of BTPageScanner */
    public BTPageScanner() {
    }
    
    /**
     * @param args the command line arguments
     */
    public static void main(String[] args) {
        // TODO code application logic here
        try {
            BTPageScanner scanner = new BTPageScanner();
            byte[] buffer = new byte[pagesize];
            java.io.RandomAccessFile index = new java.io.RandomAccessFile(args[0],"r");
            index.seek(0);
            System.out.println(index.read(buffer,0,pagesize));
            scanner.readMetaPage(buffer);
            scanner.writePageBytes(buffer);
        } catch ( IOException ioe ) {
            ioe.printStackTrace();
            
        }
    }
    
    public void readMetaPage(byte[] buffer) {
/*
 typedef struct BTMetaPageData
{
        uint32		btm_magic;
        uint32		btm_version;
        BlockNumber btm_root;
        int32		btm_level;
} BTMetaPageData;
 */
        int btm_magic = readInt(buffer,0);
        int btm_version = readInt(buffer,4);
        long btm_root = readLong(buffer,8);
        int btm_level = readInt(buffer,16);
        System.out.println(btm_magic);
        System.out.println(btm_version);
        System.out.println(btm_root);
        System.out.println(btm_level);
    }
    
    public void readOpaqueData(byte[] buffer) {
//typedef struct BTPageOpaqueData
//{
//	BlockNumber btpo_prev;		/* used for backward index scans */
//	BlockNumber btpo_next;		/* used for forward index scans */
//	BlockNumber btpo_parent;	/* pointer to parent, but not updated on
//								 * parent split */
//	uint16		btpo_flags;		/* LEAF?, ROOT?, FREE?, META?, REORDER? */
//
////} BTPageOpaqueData;
        long btpo_prev = readLong(buffer,0);
        long btpo_next = readLong(buffer,8);
        long btpo_parent = readLong(buffer,16);
        short btpo_flags = readShort(buffer,24);
    }
    
    public void writePageBytes(byte[] buffer) {
        int count = 0;
        for ( byte val : buffer ) {
            int high = (val & 0x00f0 ) >> 8;
             int low = (val & 0x000f );
             if ( high == 3 && low == 1 ) {
                 System.out.print(Character.forDigit(high,16));
                System.out.print(Character.forDigit(low,16));
             }
//             System.out.print(" ");
//            if ( count++ % 20 == 19) System.out.println();
        }
    }
    
    public long readLong(byte[] buffer,int offset) {
        long val = 0;
        
        for (int x=0;x<8;x++) {
            val |= buffer[offset + x];
            val <<= 8;
        }
        
        return val;
    }
    
    public int readInt(byte[] buffer,int offset) {
        int val = 0;
        
        for (int x=0;x<4;x++) {
            val |= buffer[offset + x];
            val <<= 8;
        }
        
        return val;
    }
    public short readShort(byte[] buffer,int offset) {
        short val = 0;
        
        for (int x=0;x<2;x++) {
            val |= buffer[offset + x];
            val <<= 8;
        }
        
        return val;
    }
}
