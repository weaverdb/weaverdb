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

extern void initweaverbackend(char* dbname);
extern bool isinitialized();
extern void prepareforshutdown(void);
extern void wrapupweaverbackend(void);
extern void singleusershutdown(int code);

extern char* GetProperty(char* key);
extern int GetIntProperty(char* key);
extern double GetFloatProperty(char* key);
extern bool PropertyIsValid(char* key);
extern int GetMaxBackends();

#ifdef __cplusplus
}
#endif

#endif
