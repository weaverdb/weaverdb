
#include <jni.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#include "main.h"


int main(int argc,char* argv[])
{
       JavaVM *jvm;       /* denotes a Java VM */
       void *henv;       /* pointer to native method interface */
       JNIEnv*	env;
        char* classes = getenv("CLASSPATH");
        char		pclass[4096];
        char		plibs[4096];
        int err = 0;
        int extras = argc - 2;
/*
        printf("extras = %d\n",extras);
*/
        if ( extras < 0 ) extras = 0;

        if ( classes == NULL || strlen(classes) == 0 ) {
            printf("CLASSPATH must be set");
            exit(1);
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
			pid_t  w = fork();
			if ( w == 0 ) {
				setsid();
			} else {
				printf("database process id: %d\n",w);
				exit(0);
			}
		} 

      JavaVMInitArgs vm_args;
       JavaVMOption options[15];

       memset(pclass,0,sizeof(pclass));
       memset(plibs,0,sizeof(plibs));
       
       sprintf(pclass,"-Djava.class.path=%s",classes);
       sprintf(plibs,"-Djava.library.path=%s",getenv("LD_LIBRARY_PATH"));

       options[0].optionString = pclass; /* user classes */
       options[0].extraInfo = NULL;                  
       options[1].optionString = plibs;  /* set native library path */
       options[1].extraInfo = NULL;    
                     
	for (;extras > 0;extras--) {
	        options[1+extras].optionString = argv[extras];
                options[1+extras].extraInfo = NULL;    
	}

       vm_args.version = JNI_VERSION_1_2;
       vm_args.options = options;
       vm_args.nOptions = argc;   /*  argc minus the program name "pgjava" and the properties file + the extras and the 
                                    *    classpath librarypath added here 
                                    */
       vm_args.ignoreUnrecognized = JNI_FALSE;
       
        /* load and initialize a Java VM, return a JNI interface 
         * pointer in env */
        err = JNI_CreateJavaVM(&jvm, &henv, &vm_args);
/*
        printf("create err %d\n",err);
*/
        env = (JNIEnv*)henv;
        jvm->AttachCurrentThread((void**)&env,NULL);

        /* invoke the Main.test method using the JNI */
        jclass cls = env->FindClass("com/myosyn/server/FrameListener");
        jmethodID mid = env->GetStaticMethodID(cls, "main", "([Ljava/lang/String;)V");
        jclass string = env->FindClass("java/lang/String");
/*
        printf("configuration file:%s\n",argv[argc-1]);
*/
        jobject pop = env->NewStringUTF(argv[argc-1]);
        jobjectArray job = env->NewObjectArray(1,string,pop);
        env->CallStaticVoidMethod(cls, mid, job);
        
        jvm->DetachCurrentThread();
        /* We are done. */

        jvm->DestroyJavaVM();
}

