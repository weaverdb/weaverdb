/*-------------------------------------------------------------------------
 *
 *
 *
 *
 * IDENTIFICATION
*		Myron Scott, mkscott@sacadia.com, 2.05.2001 
 *
 *-------------------------------------------------------------------------
 */

#ifndef _CONNECTION_UTIL_H_
#define _CONNECTION_UTIL_H_

#include "c.h"

#ifdef __cplusplus
extern "C" {
#endif

LIB_EXTERN bool initweaverbackend(char* dbname);
LIB_EXTERN bool isinitialized(void);
LIB_EXTERN bool prepareforshutdown(void);
LIB_EXTERN void wrapupweaverbackend(void);
LIB_EXTERN void singleusershutdown(int code);

LIB_EXTERN char* GetProperty(char* key);
LIB_EXTERN bool GetBoolProperty(char* key);
LIB_EXTERN int GetIntProperty(char* key);
LIB_EXTERN double GetFloatProperty(char* key);
LIB_EXTERN bool PropertyIsValid(char* key);
LIB_EXTERN int GetMaxBackends(void);
LIB_EXTERN int GetProcessorCount(void);

#ifdef __cplusplus
}
#endif

#endif
