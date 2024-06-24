
#include <jni.h>

#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#include "main.h"

static int cleanupTempDir(char* path);

int main(int argc,char* argv[])
{
       JavaVM *jvm;       /* denotes a Java VM */
       void *henv;       /* pointer to native method interface */
       JNIEnv*	env;
        char* classes = getenv("CLASSPATH");
        char		pclass[4096];
        char		plibs[4096];
        char		ptemp[256];
        char            tmpdir[256];
        
        int err = 0;
        int extras = argc - 2;

        if ( extras < 0 ) extras = 0;

        if ( classes == NULL || strlen(classes) == 0 ) {
            classes = malloc(4096);
            char* mtpg = getenv("MTPG");
            if ( mtpg == NULL || strlen(classes) == 0 ) {
                mtpg = "mtpg";
            }
            sprintf(classes,"%s/server/base_server.jar:%s/server/lib/basedata.jar:%s/lib/weaver.jar",mtpg,mtpg,mtpg);
        }

        /*  set up memory allocation mtpgjava does 8k blocks so lets use that
	as our fast value  */
#ifndef MACOSX
/*
	mallopt(M_MXFAST,256);
	mallopt(M_NLBLKS,100);
*/
#endif	

      	if (strstr(argv[0],"weaver_server") != NULL ) {
            setsid();

            pid_t  w = fork();
            if ( w == 0 ) {
                setsid();
            } else {
                printf("database process id: %d\n",w);
                exit(0);
            }
        }
        
        sprintf(tmpdir,"/var/tmp/weaver-%d",getpid());

       JavaVMInitArgs vm_args;
       JavaVMOption options[15];

       memset(pclass,0,sizeof(pclass));
       memset(plibs,0,sizeof(plibs));
       memset(ptemp,0,sizeof(ptemp));
       
       sprintf(pclass,"-Djava.class.path=%s",classes);
       sprintf(plibs,"-Djava.library.path=%s",getenv("LD_LIBRARY_PATH"));
       sprintf(ptemp,"-Djava.io.tmpdir=%s",tmpdir);

       options[0].optionString = pclass; /* user classes */
       options[0].extraInfo = NULL;                  
       options[1].optionString = plibs;  /* set native library path */
       options[1].extraInfo = NULL;    
       options[2].optionString = ptemp;  /* set tempdir path */
       options[2].extraInfo = NULL;

	for (;extras > 0;extras--) {
	        options[2+extras].optionString = argv[extras];
                options[2+extras].extraInfo = NULL;
	}

       vm_args.version = JNI_VERSION_10;
       vm_args.options = options;
       vm_args.nOptions = 3 + argc - 2;
       /*  extras +
        *      ( classpath + libpath + temppath )
        */
       vm_args.ignoreUnrecognized = JNI_FALSE;
       
        /* load and initialize a Java VM, return a JNI interface 
         * pointer in env */
        err = JNI_CreateJavaVM(&jvm, &henv, &vm_args);
        if (err != 0) {
                printf("create err %d\n",err);
                return err;
        }
        env = (JNIEnv*)henv;
        (*jvm)->AttachCurrentThread(jvm,(void**)&env,NULL);

        /* invoke the Main.test method using the JNI */
        jclass cls = (*env)->FindClass(env,"com/myosyn/server/FrameListener");
        jmethodID mid = (*env)->GetStaticMethodID(env,cls, "main", "([Ljava/lang/String;)V");
        jclass string = (*env)->FindClass(env,"java/lang/String");
/*
        printf("configuration file:%s\n",argv[argc-1]);
*/
        jobject pop = (*env)->NewStringUTF(env,argv[argc-1]);
        jobjectArray job = (*env)->NewObjectArray(env,1,string,pop);

        mkdir(tmpdir,S_IRWXU);

        (*env)->CallStaticVoidMethod(env,cls, mid, job);

        int test = cleanupTempDir(tmpdir);
        
        (*jvm)->DetachCurrentThread(jvm);
        /* We are done. */
       (*jvm)->DestroyJavaVM(jvm);
       return 0;
}

static int cleanupTempDir(char* path) {
	FILE *p;
        char msg[512];
#ifdef SUNOS
        sprintf(msg,"/usr/bin/rm -r %s",path);
#elif defined(LINUX)
        sprintf(msg,"/bin/rm -r %s",path);
#endif
	if ((p = popen(msg, "r")) == NULL ) {
            perror("SYSTEM FAILURE");
		return (-1);
        }

        while (fgets(msg, 512, p) != NULL)
             printf("%s", msg);

        return (pclose(p));
}

