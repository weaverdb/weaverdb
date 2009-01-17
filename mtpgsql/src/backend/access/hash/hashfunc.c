/*-------------------------------------------------------------------------
 *
 * hashfunc.c
 *	  Comparison functions for hash access method.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/access/hash/hashfunc.c,v 1.1.1.1 2006/08/12 00:19:57 synmscott Exp $
 *
 * NOTES
 *	  These functions are stored in pg_amproc.	For each operator class
 *	  defined on hash tables, they compute the hash value of the argument.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/hash.h"

uint32
hashint2(int16 key)
{
	return (uint32) ~key;
}

uint32
hashint4(uint32 key)
{
	return ~key;
}

uint32
hashint8(int64 *key)
{
	return ~((uint32) *key);
}

/* Hash function from Chris Torek. */
uint32
hashfloat4(float32 keyp)
{
	int			len;
	int			loop;
	uint32		h;
	char	   *kp = (char *) keyp;

	len = sizeof(float32data);

#define HASH4a	 h = (h << 5) - h + *kp++;
#define HASH4b	 h = (h << 5) + h + *kp++;
#define HASH4 HASH4b


	h = 0;
	if (len > 0)
	{
		loop = (len + 8 - 1) >> 3;

		switch (len & (8 - 1))
		{
			case 0:
				do
				{				/* All fall throughs */
					HASH4;
			case 7:
					HASH4;
			case 6:
					HASH4;
			case 5:
					HASH4;
			case 4:
					HASH4;
			case 3:
					HASH4;
			case 2:
					HASH4;
			case 1:
					HASH4;
				} while (--loop);
		}
	}
	return h;
}


uint32
hashfloat8(float64 keyp)
{
	int			len;
	int			loop;
	uint32		h;
	char	   *kp = (char *) keyp;

	len = sizeof(float64data);

#define HASH4a	 h = (h << 5) - h + *kp++;
#define HASH4b	 h = (h << 5) + h + *kp++;
#define HASH4 HASH4b


	h = 0;
	if (len > 0)
	{
		loop = (len + 8 - 1) >> 3;

		switch (len & (8 - 1))
		{
			case 0:
				do
				{				/* All fall throughs */
					HASH4;
			case 7:
					HASH4;
			case 6:
					HASH4;
			case 5:
					HASH4;
			case 4:
					HASH4;
			case 3:
					HASH4;
			case 2:
					HASH4;
			case 1:
					HASH4;
				} while (--loop);
		}
	}
	return h;
}


uint32
hashoid(Oid key)
{
	return (uint32) ~key;
}

uint32
hashoidvector(Oid *key)
{
	int			i;
	uint32		result = 0;

	for (i = INDEX_MAX_KEYS; --i >= 0;)
		result = (result << 1) ^ (~(uint32) key[i]);
	return result;
}

/*
 * Note: hashint2vector currently can't be used as a user hash table
 * hash function, because it has no pg_proc entry.	We only need it
 * for catcache indexing.
 */
uint32
hashint2vector(int16 *key)
{
	int			i;
	uint32		result = 0;

	for (i = INDEX_MAX_KEYS; --i >= 0;)
		result = (result << 1) ^ (~(uint32) key[i]);
	return result;
}


#define PRIME1			37
#define PRIME2			1048583

uint32
hashchar(char key)
{
	uint32		h;

	/* Convert char to integer */
	h = (key - ' ');
	h %= PRIME2;

	return h;
}


uint32
hashname(NameData *n)
{
	uint32		h;
	int			len;
	char	   *key;

	key = NameStr(*n);

	h = 0;
	len = NAMEDATALEN;
	/* Convert string to integer */
	while (len--)
		h = h * PRIME1 ^ (*key++ - ' ');
	h %= PRIME2;

	return h;
}



/*
 * (Comment from the original db3 hashing code: )
 *
 * "This is INCREDIBLY ugly, but fast.  We break the string up into 8 byte
 * units.  On the first time through the loop we get the 'leftover bytes'
 * (strlen % 8).  On every other iteration, we perform 8 HASHC's so we handle
 * all 8 bytes.  Essentially, this saves us 7 cmp & branch instructions.  If
 * this routine is heavily used enough, it's worth the ugly coding.
 *
 * "OZ's original sdbm hash"
 */
uint32
hashtext(struct varlena * key)
{
	int			keylen;
	char	   *keydata;
	uint32		n;
	int			loop;

	keydata = VARDATA(key);
	keylen = VARSIZE(key);

	/* keylen includes the four bytes in which string keylength is stored */
	keylen -= sizeof(VARSIZE(key));

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
