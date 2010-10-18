/*
 * WeaverAdapter.java
 *
 * Created on October 17, 2006, 11:37 AM
 *
 * To change this template, choose Tools | Template Manager
 * and open the template in the editor.
 */

package driver.weaver.connector;

import driver.weaver.WeaverInitializer;
import java.util.Properties;
import javax.resource.ResourceException;
import javax.resource.spi.*;
import javax.resource.spi.endpoint.MessageEndpointFactory;
import javax.resource.spi.work.Work;
import javax.resource.spi.work.WorkException;
import javax.resource.spi.work.WorkManager;
import javax.transaction.xa.XAResource;

/**
 *
 * @author mscott
 */
public class WeaverAdapter implements ResourceAdapter {
    private WeaverInitializer main_driver = new WeaverInitializer();
    Properties props;
    
    /** Creates a new instance of WeaverAdapter */
    public WeaverAdapter() {
        props = new Properties();
        /* set defaults */
        props.setProperty("objectloader","driver.weaver.WeaverObjectLoader");
        props.setProperty("buffercount","1024");
        props.setProperty("debuglevel","DEBUG");
        props.setProperty("maxbackends","128");
        props.setProperty("logfile","log.txt");
        props.setProperty("nofsync","false");
        props.setProperty("stdlog","true");
        props.setProperty("enable_softcommits","false");
        props.setProperty("transcareful","false");
        props.setProperty("servertype","private");
        props.setProperty("maxgrouptrans","8192");
        props.setProperty("waittime","600");
        props.setProperty("synctimeout","10000");
        props.setProperty("maxlogcount","10000");
        props.setProperty("vfdmultiple","32");
        props.setProperty("vfdblockcount","1024");
        props.setProperty("vfdsharemax","1");
        props.setProperty("vfdlogfile","pg_shadowlog");
        props.setProperty("gcsizefactor","1");
        props.setProperty("gcupdatefactor","16");
        props.setProperty("force","false");
        props.setProperty("usegc","true");
        props.setProperty("disable_crc","true");
        props.setProperty("delegatedindexbuild","false");
        props.setProperty("fastindexbuild","true");
        props.setProperty("vfdoptimize","false");
        props.setProperty("lingeringbuffers","true");
        props.setProperty("delegatedtransfermax","32768");
        props.setProperty("start_delay","0");
        /*  no default for datadir */
        //    props.setProperty("datadir","");
    }
    
    public void start(BootstrapContext boot) throws ResourceAdapterInternalException {
        WorkManager wm = boot.getWorkManager();
        try {
            wm.startWork(new InitWork());
        } catch ( WorkException exp ) {
            exp.printStackTrace();
        }
    }
    
    public void stop() {
    }
    
    public void endpointActivation(MessageEndpointFactory messageEndpointFactory, ActivationSpec activationSpec) throws ResourceException {
    }
    
    public void endpointDeactivation(MessageEndpointFactory messageEndpointFactory, ActivationSpec activationSpec) {
    }
    
    public XAResource[] getXAResources(ActivationSpec[] activationSpec) throws ResourceException {
        return null;
    }
    
    
    class InitWork implements Work {
        public void release() {
            props = null;
        }
        
        public void run() {
            main_driver.initialize(props);
        }
        
    }
    
    
    public String getObjectloader() {
        return props.getProperty("objectloader","driver.weaver.WeaverObjectLoader");
    }
    
    public void setObjectloader(String objectloader) {
        props.setProperty("objectloader",objectloader);
    }
    
    public int getBuffercount() {
        return Integer.parseInt(props.getProperty("buffercount","1024"));
    }
    
    public void setBuffercount(int buffercount) {
        props.setProperty("buffercount",Integer.toString(buffercount));
    }
    
    public String getDebuglevel() {
        return props.getProperty("debuglevel");
    }
    
    public void setDebuglevel(String debuglevel) {
        props.setProperty("debuglevel",debuglevel);
    }
    
    public int getMaxbackends() {
        return Integer.parseInt(props.getProperty("maxbackends"));
    }
    
    public void setMaxbackends(int maxbackends) {
        props.setProperty("maxbackends",Integer.toString(maxbackends));
    }
    
    public String getLogfile() {
        return props.getProperty("logfile");
    }
    
    public void setLogfile(String logfile) {
        props.setProperty("logfile",logfile);
    }
    
    public boolean isNofsync() {
        return Boolean.parseBoolean(props.getProperty("nofsync"));
        
    }
    
    public void setNofsync(boolean nofsync) {
        props.setProperty("nofsync",Boolean.toString(nofsync));
    }
    
    public boolean isStdlog() {
        return Boolean.parseBoolean(props.getProperty("stdlog"));
    }
    
    public void setStdlog(boolean stdlog) {
        props.setProperty("stdlog",Boolean.toString(stdlog));
    }
    
    public boolean isEnable_softcommits() {
        return Boolean.parseBoolean(props.getProperty("enable_softcommits"));
    }
    
    public void setEnable_softcommits(boolean enable_softcommits) {
        props.setProperty("enable_softcommits",Boolean.toString(enable_softcommits));
    }
    
    public boolean isTranscareful() {
        return Boolean.parseBoolean(props.getProperty("transcareful"));
    }
    
    public void setTranscareful(boolean transcareful) {
        props.setProperty("transcareful",Boolean.toString(transcareful));
    }
    
    public String getServertype() {
        return props.getProperty("servertype");
    }
    
    public void setServertype(String servertype) {
        props.setProperty("servertype",servertype);
    }
    
    public int getMaxgrouptrans() {
        return Integer.parseInt(props.getProperty("maxgrouptrans"));
    }
    
    public void setMaxgrouptrans(int maxgrouptrans) {
        props.setProperty("maxgrouptrans",Integer.toString(maxgrouptrans));
    }
    
    public int getWaittime() {
        return Integer.parseInt(props.getProperty("waittime"));
    }
    
    public void setWaittime(int waittime) {
        props.setProperty("waittime",Integer.toString(waittime));
    }
    
    public int getSynctimeout() {
        return Integer.parseInt(props.getProperty("synctimeout"));
    }
    
    public void setSynctimeout(int synctimeout) {
        props.setProperty("synctimeout",Integer.toString(synctimeout));
    }
    
    public int getMaxlogcount() {
        return Integer.parseInt(props.getProperty("maxlogcount"));
    }
    
    public void setMaxlogcount(int maxlogcount) {
        props.setProperty("maxlogcount",Integer.toString(maxlogcount));
    }
    
    public int getVfdmultiple() {
        return Integer.parseInt(props.getProperty("vfdmultiple"));
    }
    
    public void setVfdmultiple(int vfdmultiple) {
        props.setProperty("vfdmultiple",Integer.toString(vfdmultiple));
    }
    
    public int getVfdblockcount() {
        return Integer.parseInt(props.getProperty("vfdblockcount"));
    }
    
    public void setVfdblockcount(int vfdblockcount) {
        props.setProperty("vfdblockcount",Integer.toString(vfdblockcount));
    }
    
    public int getVfdsharemax() {
        return Integer.parseInt(props.getProperty("vfdsharemax"));
    }
    
    public void setVfdsharemax(int vfdsharemax) {
        props.setProperty("vfdsharemax",Integer.toString(vfdsharemax));
    }
    
    public String getVfdlogfile() {
        return props.getProperty("vfdlogfile");
    }
    
    public void setVfdlogfile(String vfdlogfile) {
        props.setProperty("vfdlogfile",vfdlogfile);
    }
    
    public int getGcsizefactor() {
        return Integer.parseInt(props.getProperty("gcsizefactor"));
    }
    
    public void setGcsizefactor(int gcsizefactor) {
        props.setProperty("gcsizefactor",Integer.toString(gcsizefactor));
    }
    
    public int getGcupdatefactor() {
        return Integer.parseInt(props.getProperty("gcupdatefactor"));
    }
    
    public void setGcupdatefactor(int gcupdatefactor) {
        props.setProperty("gcupdatefactor",Integer.toString(gcupdatefactor));
    }
    
    public boolean isForce() {
        return Boolean.parseBoolean(props.getProperty("force"));
    }
    
    public void setForce(boolean force) {
        props.setProperty("force",Boolean.toString(force));
    }
    
    public boolean isUsegc() {
        return Boolean.parseBoolean(props.getProperty("usegc"));
    }
    
    public void setUsegc(boolean usegc) {
        props.setProperty("usegc",Boolean.toString(usegc));
    }
    
    public boolean isDisable_crc() {
        return Boolean.parseBoolean(props.getProperty("disable_crc"));
    }
    
    public void setDisable_crc(boolean disable_crc) {
        props.setProperty("disable_crc",Boolean.toString(disable_crc));
    }
    
    public boolean isDelegatedindexbuild() {
        return Boolean.parseBoolean(props.getProperty("delegatedindexbuild"));
    }
    
    public void setDelegatedindexbuild(boolean delegatedindexbuild) {
        props.setProperty("delegatedindexbuild",Boolean.toString(delegatedindexbuild));
    }
    
    public boolean isFastindexbuild() {
        return Boolean.parseBoolean(props.getProperty("fastindexbuild"));
    }
    
    public void setFastindexbuild(boolean fastindexbuild) {
        props.setProperty("fastindexbuild",Boolean.toString(fastindexbuild));
    }
    
    public boolean isVfdoptimize() {
        return Boolean.parseBoolean(props.getProperty("vfdoptimize"));
    }
    
    public void setVfdoptimize(boolean vfdoptimize) {
        props.setProperty("vfdoptimize",Boolean.toString(vfdoptimize));
    }
    
    public boolean isLingeringbuffers() {
        return Boolean.parseBoolean(props.getProperty("lingeringbuffers"));
    }
    
    public void setLingeringbuffers(boolean lingeringbuffers) {
        props.setProperty("lingeringbuffers",Boolean.toString(lingeringbuffers));
    }
    
    public int getDelegatedtransfermax() {
        return Integer.parseInt(props.getProperty("delegatedtransfermax"));
        
    }
    
    public void setDelegatedtransfermax(int delegatedtransfermax) {
        props.setProperty("delegatedtransfermax",Integer.toString(delegatedtransfermax));
    }
    
    public int getStart_delay() {
        return Integer.parseInt(props.getProperty("start_delay"));
    }
    
    public void setStart_delay(int start_delay) {
        props.setProperty("start_delay",Integer.toString(start_delay));
    }
    
    public String getDatadir() {
        return props.getProperty("datadir");
    }
    
    public void setDatadir(String datadir) {
        props.setProperty("datadir",datadir);
    }
}
