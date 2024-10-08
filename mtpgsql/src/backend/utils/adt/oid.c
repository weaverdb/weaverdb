/*-------------------------------------------------------------------------
 *
 * oid.c
 *	  Functions for the built-in type Oid.
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


#include <ctype.h>



#include "postgres.h"

#include "utils/builtins.h"
/*****************************************************************************
 *	 USER I/O ROUTINES														 *
 *****************************************************************************/

/*
 *		oidvectorin			- converts "num num ..." to internal form
 *
 *		Note:
 *				Fills any nonexistent digits with NULL oids.
 */
Oid *
oidvectorin(char *oidString)
{
	Oid		   *result;
	int			slot;

	if (oidString == NULL)
		return NULL;

	result = (Oid *) palloc(sizeof(Oid[INDEX_MAX_KEYS]));

	for (slot = 0; *oidString && slot < INDEX_MAX_KEYS; slot++)
	{
        long home;
        if ( sscanf(oidString, "%lu",&home) == -1)
            break;
        result[slot] = (Oid)home;
        
		while (*oidString && isspace(*oidString))
			oidString++;
		while (*oidString && !isspace(*oidString))
			oidString++;
	}
	while (*oidString && isspace(*oidString))
		oidString++;
	if (*oidString)
		elog(ERROR, "oidvector value has too many values");
	while (slot < INDEX_MAX_KEYS)
		result[slot++] = 0;

	return result;
}

/*
 *		oidvectorout - converts internal form to "num num ..."
 */
char *
oidvectorout(Oid *oidArray)
{
	int			num,
				maxnum;
	char	   *rp;
	char	   *result;

	if (oidArray == NULL)
	{
		result = (char *) palloc(2);
		result[0] = '-';
		result[1] = '\0';
		return result;
	}

	/* find last non-zero value in vector */
	for (maxnum = INDEX_MAX_KEYS - 1; maxnum >= 0; maxnum--)
		if (oidArray[maxnum] != 0)
			break;

	/* assumes sign, 10 digits, ' ' */
	rp = result = (char *) palloc((maxnum + 1) * 12 + 1);
	for (num = 0; num <= maxnum; num++)
	{
        int64 process;
		if (num != 0)
			*rp++ = ' ';
		process = (int64)oidArray[num];
        lltoa(process, rp);
		while (*++rp != '\0')
			;
	}
	*rp = '\0';
	return result;
}

Oid
oidin(char *s)
{
    Oid oid = 0;
    oid = (Oid)longin(s);
    return oid;
}

char *
oidout(Oid o)
{
	return longout(o);
}

long
longin(char *s)
{
#ifdef MACOSX
    long send = (long)atoi(s);
#else
	long send = (long)atoll(s);
#endif
    return send;
}

char*
longout(long l)
{
    long long process = (long long)l;
    char* data = palloc(24);
    memset(data,0x00,24);
    lltoa(process,data);
    return data;
}

/*****************************************************************************
 *	 PUBLIC ROUTINES														 *
 *****************************************************************************/

/*
 * If you change this function, change heap_keytest()
 * because we have hardcoded this in there as an optimization
 */
bool
oideq(Oid arg1, Oid arg2)
{
	return arg1 == arg2;
}

bool
oidne(Oid arg1, Oid arg2)
{
	return arg1 != arg2;
}

bool
oidvectoreq(Oid *arg1, Oid *arg2)
{
	return (bool) (memcmp(arg1, arg2, INDEX_MAX_KEYS * sizeof(Oid)) == 0);
}

bool
oidvectorne(Oid *arg1, Oid *arg2)
{
	return (bool) (memcmp(arg1, arg2, INDEX_MAX_KEYS * sizeof(Oid)) != 0);
}

bool
oidvectorlt(Oid *arg1, Oid *arg2)
{
	int			i;

	for (i = 0; i < INDEX_MAX_KEYS; i++)
		if (!int4eq(arg1[i], arg2[i]))
			return int4lt(arg1[i], arg2[i]);
	return false;
}

bool
oidvectorle(Oid *arg1, Oid *arg2)
{
	int			i;

	for (i = 0; i < INDEX_MAX_KEYS; i++)
		if (!int4eq(arg1[i], arg2[i]))
			return int4le(arg1[i], arg2[i]);
	return true;
}

bool
oidvectorge(Oid *arg1, Oid *arg2)
{
	int			i;

	for (i = 0; i < INDEX_MAX_KEYS; i++)
		if (!int4eq(arg1[i], arg2[i]))
			return int4ge(arg1[i], arg2[i]);
	return true;
}

bool
oidvectorgt(Oid *arg1, Oid *arg2)
{
	int			i;

	for (i = 0; i < INDEX_MAX_KEYS; i++)
		if (!int4eq(arg1[i], arg2[i]))
			return int4gt(arg1[i], arg2[i]);
	return false;
}

bool
oideqlong(Oid arg1, long arg2)
{
    return arg2 >=0 && arg1 == arg2;
}

bool
longeqoid(long arg1, Oid arg2)
{
    return arg1 >= 0 && arg1 == arg2;
}

bool
oideqint4(Oid arg1, int32 arg2)
{
/* oid is unsigned, but int8 is signed */
	return arg2 >= 0 && arg1 == arg2;
}

bool
int4eqoid(int32 arg1, Oid arg2)
{
/* oid is unsigned, but int8 is signed */
	return arg1 >= 0 && arg1 == arg2;
}

text *
oid_text(Oid oid)
{
	text	   *result;

	int			len;
	char	   *str;

	str = oidout(oid);
	len = (strlen(str) + VARHDRSZ);

	result = palloc(len);

	SETVARSIZE(result,len);
	memmove(VARDATA(result), str, (len - VARHDRSZ));
	pfree(str);

	return result;
}	/* oid_text() */

Oid
text_oid(text *string)
{
	Oid			result;

	int			len;
	char	   *str;

	len = (VARSIZE(string) - VARHDRSZ);

	str = palloc(len + 1);
	memmove(str, VARDATA(string), len);
	*(str + len) = '\0';

	result = oidin(str);
	pfree(str);

	return result;
}	/* oid_text() */
