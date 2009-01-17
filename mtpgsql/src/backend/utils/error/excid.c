/*-------------------------------------------------------------------------
 *
 * excid.c
 *	  POSTGRES known exception identifier code.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/utils/error/excid.c,v 1.1.1.1 2006/08/12 00:21:58 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

/*****************************************************************************
 *	 Generic Recoverable Exceptions											 *
 *****************************************************************************/


/*
 * FailedAssertion
 *		Indicates an Assert(...) failed.
 */
Exception	FailedAssertion = {"Failed Assertion"};

/*
 * BadState
 *		Indicates a function call request is inconsistent with module state.
 */
Exception	BadState = {"Bad State for Function Call"};

/*
 * BadArg
 *		Indicates a function call argument or arguments is out-of-bounds.
 */
Exception	BadArg = {"Bad Argument to Function Call"};

/*****************************************************************************
 *	 Specific Recoverable Exceptions										 *
 *****************************************************************************/

/*
 * BadAllocSize
 *		Indicates that an allocation request is of unreasonable size.
 */
Exception	BadAllocSize = {"Too Large Allocation Request"};

/*
 * ExhaustedMemory
 *		Indicates an dynamic memory allocation failed.
 */
Exception	ExhaustedMemory = {"Memory Allocation Failed"};

/*
 * Unimplemented
 *		Indicates a function call request requires unimplemented code.
 */
Exception	Unimplemented = {"Unimplemented Functionality"};

Exception	CatalogFailure = {"Catalog failure"};		/* XXX inconsistent */
Exception	InternalError = {"Internal Error"}; /* XXX inconsistent */
Exception	SemanticError = {"Semantic Error"}; /* XXX inconsistent */
Exception	SystemError = {"System Error"};		/* XXX inconsistent */
