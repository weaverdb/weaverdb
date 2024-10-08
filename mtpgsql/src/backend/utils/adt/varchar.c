/*-------------------------------------------------------------------------
 *
 * varchar.c
 *	  Functions for the built-in type char() and varchar().
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

#include "access/htup.h"
#include "catalog/pg_type.h"
#include "utils/builtins.h"
#ifdef MULTIBYTE
#include "mb/pg_wchar.h"
#endif

#ifdef CYR_RECODE
char	   *convertstr(char *, int, int);

#endif


/*
 * CHAR() and VARCHAR() types are part of the ANSI SQL standard. CHAR()
 * is for blank-padded string whose length is specified in CREATE TABLE.
 * VARCHAR is for storing string whose length is at most the length specified
 * at CREATE TABLE time.
 *
 * It's hard to implement these types because we cannot figure out what
 * the length of the type from the type itself. I change (hopefully all) the
 * fmgr calls that invoke input functions of a data type to supply the
 * length also. (eg. in INSERTs, we have the tupleDescriptor which contains
 * the length of the attributes and hence the exact length of the char() or
 * varchar(). We pass this to bpcharin() or varcharin().) In the case where
 * we cannot determine the length, we pass in -1 instead and the input string
 * must be null-terminated.
 *
 * We actually implement this as a varlena so that we don't have to pass in
 * the length for the comparison functions. (The difference between "text"
 * is that we truncate and possibly blank-pad the string at insertion time.)
 *
 *															  - ay 6/95
 */


/*****************************************************************************
 *	 bpchar - char()														 *
 *****************************************************************************/

/*
 * bpcharin -
 *	  converts a string of char() type to the internal representation.
 *	  len is the length specified in () plus VARHDRSZ bytes. (XXX dummy is here
 *	  because we pass typelem as the second argument for array_in.)
 */
char *
bpcharin(char *s, int dummy, int32 atttypmod)
{
	char	   *result,
			   *r;
	int			len;
	int			i;

	if (s == NULL)
		return (char *) NULL;

	if (atttypmod < (int32) VARHDRSZ)
	{
		/* If typmod is -1 (or invalid), use the actual string length */
		len = strlen(s);
		atttypmod = len + VARHDRSZ;
	}
	else
		len = atttypmod - VARHDRSZ;

	result = (char *) palloc(atttypmod);
	SETVARSIZE(result,atttypmod);
	r = VARDATA(result);
	for (i = 0; i < len; i++, r++, s++)
	{
		*r = *s;
		if (*r == '\0')
			break;
	}

#ifdef CYR_RECODE
	convertstr(result + VARHDRSZ, len, 0);
#endif

	/* blank pad the string if necessary */
	for (; i < len; i++)
		*r++ = ' ';
	return result;
}

char *
bpcharout(char *s)
{
	char	   *result;
	int			len;

	if (s == NULL)
	{
		result = (char *) palloc(2);
		result[0] = '-';
		result[1] = '\0';
	}
	else
	{
		len = VARSIZE(s) - VARHDRSZ;
		result = (char *) palloc(len + 1);
		StrNCpy(result, VARDATA(s), len + 1);	/* these are blank-padded */
	}

#ifdef CYR_RECODE
	convertstr(result, len, 1);
#endif

	return result;
}

/* bpchar()
 * Converts a char() type to a specific internal length.
 * len is the length specified in () plus VARHDRSZ bytes.
 */
char *
bpchar(char *s, int32 len)
{
	char	   *result,
			   *r;
	int			rlen,
				slen;
	int			i;

	if (s == NULL)
		return (char *) NULL;

	/* No work if typmod is invalid or supplied data matches it already */
	if (len < (int32) VARHDRSZ || len == VARSIZE(s))
		return s;

	rlen = len - VARHDRSZ;

#ifdef STRINGDEBUG
	printf("bpchar- convert string length %d (%d) ->%d (%d)\n",
		   VARSIZE(s) - VARHDRSZ, VARSIZE(s), rlen, len);
#endif

	result = (char *) palloc(len);
	SETVARSIZE(result,len);
	r = VARDATA(result);
#ifdef MULTIBYTE

	/*
	 * truncate multi-byte string in a way not to break multi-byte
	 * boundary
	 */
	if (VARSIZE(s) > len)
		slen = pg_mbcliplen(VARDATA(s), VARSIZE(s) - VARHDRSZ, rlen);
	else
		slen = VARSIZE(s) - VARHDRSZ;
#else
	slen = VARSIZE(s) - VARHDRSZ;
#endif
	s = VARDATA(s);

#ifdef STRINGDEBUG
	printf("bpchar- string is '");
#endif

	for (i = 0; (i < rlen) && (i < slen); i++)
	{
		if (*s == '\0')
			break;

#ifdef STRINGDEBUG
		printf("%c", *s);
#endif

		*r++ = *s++;
	}

#ifdef STRINGDEBUG
	printf("'\n");
#endif

	/* blank pad the string if necessary */
	for (; i < rlen; i++)
		*r++ = ' ';

	return result;
}	/* bpchar() */

/* _bpchar()
 * Converts an array of char() type to a specific internal length.
 * len is the length specified in () plus VARHDRSZ bytes.
 */
ArrayType  *
_bpchar(ArrayType *v, int32 len)
{
#ifdef NOARRAY
	elog(ERROR,"array map not implemented");
	return NULL;
#else
	return array_map(v, BPCHAROID, bpchar, BPCHAROID, 1, len);
#endif
}


/* bpchar_char()
 * Convert bpchar(1) to char.
 */
int32
bpchar_char(char *s)
{
	return (int32) *VARDATA(s);
}	/* bpchar_char() */

/* char_bpchar()
 * Convert char to bpchar(1).
 */
char *
char_bpchar(int32 c)
{
	char	   *result;

	result = palloc(VARHDRSZ + 1);

	SETVARSIZE(result,VARHDRSZ + 1);
	*(VARDATA(result)) = (char) c;

	return result;
}	/* char_bpchar() */


/* bpchar_name()
 * Converts a bpchar() type to a NameData type.
 */
NameData   *
bpchar_name(char *s)
{
	NameData   *result;
	int			len;

	if (s == NULL)
		return NULL;

	len = VARSIZE(s) - VARHDRSZ;
	if (len > NAMEDATALEN)
		len = NAMEDATALEN;

	while (len > 0)
	{
		if (*(VARDATA(s) + len - 1) != ' ')
			break;
		len--;
	}

#ifdef STRINGDEBUG
	printf("bpchar- convert string length %d (%d) ->%d\n",
		   VARSIZE(s) - VARHDRSZ, VARSIZE(s), len);
#endif

	result = (NameData *) palloc(NAMEDATALEN);
	StrNCpy(NameStr(*result), VARDATA(s), NAMEDATALEN);

	/* now null pad to full length... */
	while (len < NAMEDATALEN)
	{
		*(NameStr(*result) + len) = '\0';
		len++;
	}

	return result;
}	/* bpchar_name() */

/* name_bpchar()
 * Converts a NameData type to a bpchar type.
 */
char *
name_bpchar(NameData *s)
{
	char	   *result;
	int			len;

	if (s == NULL)
		return NULL;

	len = strlen(NameStr(*s));

#ifdef STRINGDEBUG
	printf("bpchar- convert string length %d (%d) ->%d\n",
		   VARSIZE(s) - VARHDRSZ, VARSIZE(s), len);
#endif

	result = (char *) palloc(VARHDRSZ + len);
	strncpy(VARDATA(result), NameStr(*s), len);
	SETVARSIZE(result,len + VARHDRSZ);

	return result;
}	/* name_bpchar() */


/*****************************************************************************
 *	 varchar - varchar()													 *
 *****************************************************************************/

/*
 * varcharin -
 *	  converts a string of varchar() type to the internal representation.
 *	  len is the length specified in () plus VARHDRSZ bytes. (XXX dummy is here
 *	  because we pass typelem as the second argument for array_in.)
 */
char *
varcharin(char *s, int dummy, int32 atttypmod)
{
	char	   *result;
	int			len;

	if (s == NULL)
		return (char *) NULL;

	len = strlen(s) + VARHDRSZ;
	if (atttypmod >= (int32) VARHDRSZ && len > atttypmod)
		len = atttypmod;		/* clip the string at max length */

	result = (char *) palloc(len);
	SETVARSIZE(result,len);
	strncpy(VARDATA(result), s, len - VARHDRSZ);

#ifdef CYR_RECODE
	convertstr(result + VARHDRSZ, len, 0);
#endif

	return result;
}

char *
varcharout(char *s)
{
	char	   *result;
	int			len;

	if (s == NULL)
	{
		result = (char *) palloc(2);
		result[0] = '-';
		result[1] = '\0';
	}
	else
	{
		len = VARSIZE(s) - VARHDRSZ;
		result = (char *) palloc(len + 1);
		StrNCpy(result, VARDATA(s), len + 1);
	}

#ifdef CYR_RECODE
	convertstr(result, len, 1);
#endif

	return result;
}

/* varchar()
 * Converts a varchar() type to the specified size.
 * slen is the length specified in () plus VARHDRSZ bytes.
 */
char *
varchar(char *s, int32 slen)
{
	char	   *result;
	int			len;

	if (s == NULL)
		return (char *) NULL;

	len = VARSIZE(s);
	if (slen < (int32) VARHDRSZ || len <= slen)
		return (char *) s;

	/* only reach here if we need to truncate string... */

#ifdef MULTIBYTE

	/*
	 * truncate multi-byte string in a way not to break multi-byte
	 * boundary
	 */
	len = pg_mbcliplen(VARDATA(s), slen - VARHDRSZ, slen - VARHDRSZ);
	slen = len + VARHDRSZ;
#else
	len = slen - VARHDRSZ;
#endif

	result = (char *) palloc(slen);
	SETVARSIZE(result,slen);
	strncpy(VARDATA(result), VARDATA(s), len);

	return result;
}	/* varchar() */

/* _varchar()
 * Converts an array of varchar() type to the specified size.
 * len is the length specified in () plus VARHDRSZ bytes.
 */
ArrayType  *
_varchar(ArrayType *v, int32 len)
{
#ifdef NOARRAY
	elog(ERROR,"array map not implemented");
	return NULL;
#else
	return array_map(v, VARCHAROID, varchar, VARCHAROID, 1, len);
#endif
}


/*****************************************************************************
 *	Comparison Functions used for bpchar
 *****************************************************************************/

static int
bcTruelen(char *arg)
{
	char	   *s = VARDATA(arg);
	int			i;
	int			len;

	len = VARSIZE(arg) - VARHDRSZ;
	for (i = len - 1; i >= 0; i--)
	{
		if (s[i] != ' ')
			break;
	}
	return i + 1;
}

int32
bpcharlen(char *arg)
{
#ifdef MULTIBYTE
	unsigned char *s;
	int			len,
				l,
				wl;

#endif
	if (!PointerIsValid(arg))
		elog(ERROR, "Bad (null) char() external representation");
#ifdef MULTIBYTE
	l = VARSIZE(arg) - VARHDRSZ;
	len = 0;
	s = VARDATA(arg);
	while (l > 0)
	{
		wl = pg_mblen(s);
		l -= wl;
		s += wl;
		len++;
	}
	return (len);
#else
	return (VARSIZE(arg) - VARHDRSZ);
#endif
}

int32
bpcharoctetlen(char *arg)
{
	if (!PointerIsValid(arg))
		elog(ERROR, "Bad (null) char() external representation");

	return (VARSIZE(arg) - VARHDRSZ);
}

bool
bpchareq(char *arg1, char *arg2)
{
	int			len1,
				len2;

	if (arg1 == NULL || arg2 == NULL)
		return (bool) 0;
	len1 = bcTruelen(arg1);
	len2 = bcTruelen(arg2);

	if (len1 != len2)
		return 0;

	return strncmp(VARDATA(arg1), VARDATA(arg2), len1) == 0;
}

bool
bpcharne(char *arg1, char *arg2)
{
	int			len1,
				len2;

	if (arg1 == NULL || arg2 == NULL)
		return (bool) 0;
	len1 = bcTruelen(arg1);
	len2 = bcTruelen(arg2);

	if (len1 != len2)
		return 1;

	return strncmp(VARDATA(arg1), VARDATA(arg2), len1) != 0;
}

bool
bpcharlt(char *arg1, char *arg2)
{
	int			len1,
				len2;
	int			cmp;

	if (arg1 == NULL || arg2 == NULL)
		return (bool) 0;
	len1 = bcTruelen(arg1);
	len2 = bcTruelen(arg2);

	cmp = varstr_cmp(VARDATA(arg1), len1, VARDATA(arg2), len2);
	if (cmp == 0)
		return len1 < len2;
	else
		return cmp < 0;
}

bool
bpcharle(char *arg1, char *arg2)
{
	int			len1,
				len2;
	int			cmp;

	if (arg1 == NULL || arg2 == NULL)
		return (bool) 0;
	len1 = bcTruelen(arg1);
	len2 = bcTruelen(arg2);

	cmp = varstr_cmp(VARDATA(arg1), len1, VARDATA(arg2), len2);
	if (0 == cmp)
		return (bool) (len1 <= len2 ? 1 : 0);
	else
		return (bool) (cmp <= 0);
}

bool
bpchargt(char *arg1, char *arg2)
{
	int			len1,
				len2;
	int			cmp;

	if (arg1 == NULL || arg2 == NULL)
		return (bool) 0;
	len1 = bcTruelen(arg1);
	len2 = bcTruelen(arg2);

	cmp = varstr_cmp(VARDATA(arg1), len1, VARDATA(arg2), len2);
	if (cmp == 0)
		return len1 > len2;
	else
		return cmp > 0;
}

bool
bpcharge(char *arg1, char *arg2)
{
	int			len1,
				len2;
	int			cmp;

	if (arg1 == NULL || arg2 == NULL)
		return (bool) 0;
	len1 = bcTruelen(arg1);
	len2 = bcTruelen(arg2);

	cmp = varstr_cmp(VARDATA(arg1), len1, VARDATA(arg2), len2);
	if (0 == cmp)
		return (bool) (len1 >= len2 ? 1 : 0);
	else
		return (bool) (cmp >= 0);
}

int32
bpcharcmp(char *arg1, char *arg2)
{
	int			len1,
				len2;
	int			cmp;

	len1 = bcTruelen(arg1);
	len2 = bcTruelen(arg2);

	cmp = varstr_cmp(VARDATA(arg1), len1, VARDATA(arg2), len2);
	if ((0 == cmp) && (len1 != len2))
		return (int32) (len1 < len2 ? -1 : 1);
	else
		return cmp;
}

/*****************************************************************************
 *	Comparison Functions used for varchar
 *****************************************************************************/

int32
varcharlen(char *arg)
{
#ifdef MULTIBYTE
	unsigned char *s;
	int			len,
				l,
				wl;

#endif
	if (!PointerIsValid(arg))
		elog(ERROR, "Bad (null) varchar() external representation");

#ifdef MULTIBYTE
	len = 0;
	s = VARDATA(arg);
	l = VARSIZE(arg) - VARHDRSZ;
	while (l > 0)
	{
		wl = pg_mblen(s);
		l -= wl;
		s += wl;
		len++;
	}
	return (len);
#else
	return VARSIZE(arg) - VARHDRSZ;
#endif
}

int32
varcharoctetlen(char *arg)
{
	if (!PointerIsValid(arg))
		elog(ERROR, "Bad (null) varchar() external representation");
	return VARSIZE(arg) - VARHDRSZ;
}

bool
varchareq(char *arg1, char *arg2)
{
	int			len1,
				len2;

	if (arg1 == NULL || arg2 == NULL)
		return (bool) 0;

	len1 = VARSIZE(arg1) - VARHDRSZ;
	len2 = VARSIZE(arg2) - VARHDRSZ;

	if (len1 != len2)
		return 0;

	return strncmp(VARDATA(arg1), VARDATA(arg2), len1) == 0;
}

bool
varcharne(char *arg1, char *arg2)
{
	int			len1,
				len2;

	if (arg1 == NULL || arg2 == NULL)
		return (bool) 0;
	len1 = VARSIZE(arg1) - VARHDRSZ;
	len2 = VARSIZE(arg2) - VARHDRSZ;

	if (len1 != len2)
		return 1;

	return strncmp(VARDATA(arg1), VARDATA(arg2), len1) != 0;
}

bool
varcharlt(char *arg1, char *arg2)
{
	int			len1,
				len2;
	int			cmp;

	if (arg1 == NULL || arg2 == NULL)
		return (bool) 0;
	len1 = VARSIZE(arg1) - VARHDRSZ;
	len2 = VARSIZE(arg2) - VARHDRSZ;

	cmp = varstr_cmp(VARDATA(arg1), len1, VARDATA(arg2), len2);
	if (cmp == 0)
		return len1 < len2;
	else
		return cmp < 0;
}

bool
varcharle(char *arg1, char *arg2)
{
	int			len1,
				len2;
	int			cmp;

	if (arg1 == NULL || arg2 == NULL)
		return (bool) 0;
	len1 = VARSIZE(arg1) - VARHDRSZ;
	len2 = VARSIZE(arg2) - VARHDRSZ;

	cmp = varstr_cmp(VARDATA(arg1), len1, VARDATA(arg2), len2);
	if (0 == cmp)
		return (bool) (len1 <= len2 ? 1 : 0);
	else
		return (bool) (cmp <= 0);
}

bool
varchargt(char *arg1, char *arg2)
{
	int			len1,
				len2;
	int			cmp;

	if (arg1 == NULL || arg2 == NULL)
		return (bool) 0;
	len1 = VARSIZE(arg1) - VARHDRSZ;
	len2 = VARSIZE(arg2) - VARHDRSZ;

	cmp = varstr_cmp(VARDATA(arg1), len1, VARDATA(arg2), len2);
	if (cmp == 0)
		return len1 > len2;
	else
		return cmp > 0;
}

bool
varcharge(char *arg1, char *arg2)
{
	int			len1,
				len2;
	int			cmp;

	if (arg1 == NULL || arg2 == NULL)
		return (bool) 0;
	len1 = VARSIZE(arg1) - VARHDRSZ;
	len2 = VARSIZE(arg2) - VARHDRSZ;

	cmp = varstr_cmp(VARDATA(arg1), len1, VARDATA(arg2), len2);
	if (0 == cmp)
		return (bool) (len1 >= len2 ? 1 : 0);
	else
		return (bool) (cmp >= 0);

}

bool
byteaeq(bytea *arg1, bytea *arg2)
{
	int			i,
                                len1,
				len2;

	if (arg1 == NULL || arg2 == NULL)
		return (bool) 0;

	len1 = VARSIZE(arg1) - VARHDRSZ;
	len2 = VARSIZE(arg2) - VARHDRSZ;

	if (len1 != len2)
		return 0;

        for (i=0;i<len1;i++) {
            if ( VARDATA(arg1)[i] != VARDATA(arg2)[i] ) return 0;
        }
        return 1;
}

bool
byteane(bytea *arg1, bytea *arg2)
{
    return !byteaeq(arg1,arg2);
}

bool
bytealt(bytea *arg1, bytea *arg2)
{
	int			len0,
                                len1,
				len2;
	int			i;

	if (arg1 == NULL && arg2 == NULL)
		return (bool) 0;
	len1 = VARSIZE(arg1) - VARHDRSZ;
	len2 = VARSIZE(arg2) - VARHDRSZ;
        len0 = ( len1 < len2 ) ? len1 : len2;
        for (i=0;i<len0;i++) {
            if ( VARDATA(arg1)[i] < VARDATA(arg2)[i] ) return 1;
        }
        if ( len1 < len2 ) return 1;
        return 0;
}

bool
byteale(bytea *arg1, bytea *arg2)
{
	int			len0,
                                len1,
				len2;
	int			i;

	if (arg1 == NULL || arg2 == NULL)
		return (bool) 0;
	len1 = VARSIZE(arg1) - VARHDRSZ;
	len2 = VARSIZE(arg2) - VARHDRSZ;
        len0 = ( len1 < len2 ) ? len1 : len2;
        
        for (i=0;i<len0;i++) {
            if ( VARDATA(arg1)[i] > VARDATA(arg2)[i] ) return 0;
        }
        if ( len1 > len2 ) return 0;
        return 1;
}

bool
byteagt(bytea *arg1, bytea *arg2)
{
    return !byteale(arg1,arg2);
}

bool
byteage(bytea *arg1, bytea *arg2)
{
	return !bytealt(arg1,arg2);

}

int32
byteacmp(bytea *arg1, bytea *arg2)
{
	int			len0,
                                len1,
				len2;
	int			cmp;

	len1 = VARSIZE(arg1) - VARHDRSZ;
	len2 = VARSIZE(arg2) - VARHDRSZ;
        len0 = ( len1 < len2 ) ? len1 : len2;
        
	cmp = memcmp(VARDATA(arg1), VARDATA(arg2), len0);
	if ((0 == cmp) && (len1 != len2))
		return (int32) (len1 < len2 ? -1 : 1);
	else
		return (int32) (cmp);
}

int32
varcharcmp(char *arg1, char *arg2)
{
	int			len1,
				len2;
	int			cmp;

	len1 = VARSIZE(arg1) - VARHDRSZ;
	len2 = VARSIZE(arg2) - VARHDRSZ;
	cmp = varstr_cmp(VARDATA(arg1), len1, VARDATA(arg2), len2);
	if ((0 == cmp) && (len1 != len2))
		return (int32) (len1 < len2 ? -1 : 1);
	else
		return (int32) (cmp);
}

/*****************************************************************************
 * Hash functions (modified from hashtext in access/hash/hashfunc.c)
 *****************************************************************************/

uint32
hashbpchar(struct varlena * key)
{
	int			keylen;
	char	   *keydata;
	uint32		n;
	int			loop;

	keydata = VARDATA(key);
	keylen = bcTruelen((char *) key);

#define HASHC	n = *keydata++ + 65599 * n

	n = 0;
	if (keylen > 0)
	{
		loop = (keylen + 8 - 1) >> 3;

		switch (keylen & (8 - 1))
		{
			case 0:
				do
				{				/* All fall throughs */
					HASHC;
			case 7:
					HASHC;
			case 6:
					HASHC;
			case 5:
					HASHC;
			case 4:
					HASHC;
			case 3:
					HASHC;
			case 2:
					HASHC;
			case 1:
					HASHC;
				} while (--loop);
		}
	}
	return n;
}

uint32
hashvarchar(struct varlena * key)
{
	int			keylen;
	char	   *keydata;
	uint32		n;
	int			loop;

	keydata = VARDATA(key);
	keylen = VARSIZE(key) - VARHDRSZ;

#define HASHC	n = *keydata++ + 65599 * n

	n = 0;
	if (keylen > 0)
	{
		loop = (keylen + 8 - 1) >> 3;

		switch (keylen & (8 - 1))
		{
			case 0:
				do
				{				/* All fall throughs */
					HASHC;
			case 7:
					HASHC;
			case 6:
					HASHC;
			case 5:
					HASHC;
			case 4:
					HASHC;
			case 3:
					HASHC;
			case 2:
					HASHC;
			case 1:
					HASHC;
				} while (--loop);
		}
	}
	return n;
}
