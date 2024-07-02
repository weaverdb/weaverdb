/*-------------------------------------------------------------------------
 *
 * hasht.h
 *	  hash table related functions that are not directly supported
 *	  under utils/hash.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef HASHT_H
#define HASHT_H

#include "utils/hsearch.h"

typedef void (*HashtFunc) (void *hashitem, int arg);

PG_EXTERN void HashTableWalk(HTAB *hashtable, HashtFunc function, long arg);

#endif	 /* HASHT_H */
