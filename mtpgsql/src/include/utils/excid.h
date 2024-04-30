/*-------------------------------------------------------------------------
 *
 * excid.h
 *	  POSTGRES known exception identifier definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: excid.h,v 1.1.1.1 2006/08/12 00:22:26 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef EXCID_H
#define EXCID_H


PG_EXTERN Exception FailedAssertion;
PG_EXTERN Exception BadState;
PG_EXTERN Exception BadArg;
PG_EXTERN Exception BadAllocSize;
PG_EXTERN Exception ExhaustedMemory;
PG_EXTERN Exception Unimplemented;

PG_EXTERN Exception CatalogFailure;/* XXX inconsistent naming style */
PG_EXTERN Exception InternalError; /* XXX inconsistent naming style */
PG_EXTERN Exception SemanticError; /* XXX inconsistent naming style */
PG_EXTERN Exception SystemError;	/* XXX inconsistent naming style */

#endif	 /* EXCID_H */
