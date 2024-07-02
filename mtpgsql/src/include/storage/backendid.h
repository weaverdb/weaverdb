/*-------------------------------------------------------------------------
 *
 * backendid.h
 *	  POSTGRES backend id communication definitions
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef BACKENDID_H
#define BACKENDID_H

/* ----------------
 *		-cim 8/17/90
 * ----------------
 */
typedef int16 BackendId;		/* unique currently active backend
								 * identifier */

#define InvalidBackendId		(-1)

typedef int32 BackendTag;		/* unique backend identifier */

#define InvalidBackendTag		(-1)
/*
extern BackendId MyBackendId;	
extern BackendTag MyBackendTag;
*/
#endif	 /* BACKENDID_H */
