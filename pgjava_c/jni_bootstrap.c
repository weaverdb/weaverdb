
#include <jni.h>

#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#include "org_weaverdb_WeaverCmdLine.h"

extern int main(int narg, char* argv[]);

JNIEXPORT jint JNICALL Java_org_weaverdb_WeaverCmdLine_cmd(JNIEnv *env, jclass clazz, jobjectArray args) {
    int x=0;
    jsize inSize = (*env)->GetArrayLength(env, args);
    char*  argv[inSize];

    for (x=0;x<inSize;x++) {
        jstring carg = (*env)->GetObjectArrayElement(env, args, x);
        argv[x] = (char*)(*env)->GetStringUTFChars(env, carg, NULL);
    }

    return main(inSize, argv);
}