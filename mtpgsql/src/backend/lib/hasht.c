/*-------------------------------------------------------------------------
 *
 * hasht.c
 *	  hash table related functions that are not directly supported
 *	  by the hashing packages under utils/hash.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "lib/hasht.h"
#include "utils/memutils.h"

/* -----------------------------------
 *		HashTableWalk
 *
 *		call function on every element in hashtable
 *		one extra argument (arg) may be supplied
 * -----------------------------------
 */
void
HashTableWalk(HTAB *hashtable, HashtFunc function,long arg)
{
    HASH_SEQ_STATUS 	stats;
    void*				entry;

    hash_seq_init(&stats,hashtable);
    
	while ((entry = hash_seq_search(&stats)) != NULL)
	{

		/*
		 * XXX the corresponding hash table insertion does NOT LONGALIGN
		 * -- make sure the keysize is ok
		 */
/*		data = (void *) LONGALIGN((char *) hashent + keysize);  */
		(*function) (entry, arg);
	}
}
