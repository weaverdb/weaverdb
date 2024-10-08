/*-------------------------------------------------------------------------
 *
 * ibit.h
 *	  POSTGRES index valid attribute bit map definitions.
 *
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef IBIT_H
#define IBIT_H

#include "catalog/pg_index.h"

typedef struct IndexAttributeBitMapData
{
	bits8		bits[(INDEX_MAX_KEYS + 8 - 1) / 8];
} IndexAttributeBitMapData;

typedef IndexAttributeBitMapData *IndexAttributeBitMap;

#define IndexAttributeBitMapSize		sizeof(IndexAttributeBitMapData)

/*
 * IndexAttributeBitMapIsValid
 *		True iff attribute bit map is valid.
 */
#define IndexAttributeBitMapIsValid(bits) PointerIsValid(bits)

#endif	 /* IBIT_H */
