/*-------------------------------------------------------------------------
 *
 *	connectionutil.h 
 *		startup/shutdown for embedding
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 *
 * IDENTIFICATION
 *	
 *
 *-------------------------------------------------------------------------
 */
/*-------------------------------------------------------------------------
 *
 *
 *
 *
 * IDENTIFICATION
 *
 *-------------------------------------------------------------------------
 */

#ifndef _CONNECTION_UTIL_H_
#define _CONNECTION_UTIL_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef C_H
#ifndef bool
typedef char bool;
#endif	 /* ndef bool */

#ifndef true
#define true	((bool) 1)
#endif

#ifndef false
#define false	((bool) 0)
#endif

#ifndef TRUE
#define TRUE	1
#endif	 /* TRUE */

#ifndef FALSE
#define FALSE	0
#endif	 /* FALSE */
#endif

LIB_EXTERN bool initweaverbackend(const char* dbname);
LIB_EXTERN bool isinitialized(void);
LIB_EXTERN bool prepareforshutdown(void);
LIB_EXTERN void wrapupweaverbackend(void);
LIB_EXTERN void singleusershutdown(int code);

#ifdef __cplusplus
}
#endif

#endif