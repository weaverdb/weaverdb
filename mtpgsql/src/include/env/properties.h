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

#ifndef _PROPERTIES_H_
#define _PROPERTIES_H_

#ifdef __cplusplus
extern "C" {
#endif

PG_EXTERN char* GetProperty(char* key);
PG_EXTERN bool GetBoolProperty(char* key);
PG_EXTERN int GetIntProperty(char* key);
PG_EXTERN double GetFloatProperty(char* key);
PG_EXTERN bool PropertyIsValid(char* key);
PG_EXTERN int GetMaxBackends(void);
PG_EXTERN int GetProcessorCount(void);

#ifdef __cplusplus
}
#endif

#endif
