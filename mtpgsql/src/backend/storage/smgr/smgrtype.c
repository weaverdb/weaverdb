/*-------------------------------------------------------------------------
 *
 * smgrtype.c
 *	  storage manager type
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
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

#include "storage/smgr.h"

typedef struct smgrid
{
	char	   *smgr_name;
} smgrid;

/*
 *	StorageManager[] -- List of defined storage managers.
 *
 *		The weird comma placement is to keep compilers happy no matter
 *		which of these is (or is not) defined.
 */

static smgrid StorageManager[] = {
	{"magnetic disk"},
	{"virtual files"},
	{"zfs dmu"},
#ifdef STABLE_MEMORY_STORAGE
	{"main memory"}
#endif
};

static int	NStorageManagers = lengthof(StorageManager);

int2
smgrin(char *s)
{
	int			i;

	for (i = 0; i < NStorageManagers; i++)
	{
		if (strcmp(s, StorageManager[i].smgr_name) == 0)
			return (int2) i;
	}
	elog(ERROR, "smgrin: illegal storage manager name %s", s);
	return 0;
}

char *
smgrout(int2 i)
{
	char	   *s;

	if (i >= NStorageManagers || i < 0)
		elog(ERROR, "Illegal storage manager id %d", i);

	s = (char *) palloc(strlen(StorageManager[i].smgr_name) + 1);
	strcpy(s, StorageManager[i].smgr_name);
	return s;
}

bool
smgreq(int2 a, int2 b)
{
	if (a == b)
		return true;
	return false;
}

bool
smgrne(int2 a, int2 b)
{
	if (a == b)
		return false;
	return true;
}
