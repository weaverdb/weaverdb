

package driver.weaver;

import com.drew.imaging.ImageMetadataReader;
import com.drew.metadata.Directory;
import com.drew.metadata.Metadata;
import com.drew.metadata.Tag;
import java.io.File;
import java.io.FileInputStream;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import org.junit.jupiter.api.extension.ExtendWith;

/**
 *
 * @author myronscott
 */

@ExtendWith({InstallNative.class})
public class ImageTest {
    
    public ImageTest() {
    }

    @org.junit.jupiter.api.BeforeAll
    public static void setUpClass() throws Exception {

    }

    @org.junit.jupiter.api.AfterAll
    public static void tearDownClass() throws Exception {

    }

    @org.junit.jupiter.api.BeforeEach
    public void setUp() throws Exception {
    }

    @org.junit.jupiter.api.AfterEach
    public void tearDown() throws Exception {
    }
    
    @org.junit.jupiter.api.Test
    public void test() throws Exception {
        Path p = Paths.get(System.getProperty("user.dir"), "pokemon", "images");
        DirectoryStream<Path> paths = Files.newDirectoryStream(p, (fp)->{
            System.out.println(fp.toString());
            return !fp.endsWith(".png");
        });
        for (Path img : paths) {
            File f = img.toFile();
            System.out.println(f.getName());
            try (FileInputStream is = new FileInputStream(f)) {
                Metadata md = ImageMetadataReader.readMetadata(is);
                for (Directory d : md.getDirectories()) {
                    System.out.println("\t" + d.getName());
                    for (Tag t : d.getTags()) {
                        System.out.println("\t\t" + t.toString());
                    }
                }
            }
        }
    }
}
