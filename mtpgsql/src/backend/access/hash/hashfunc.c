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
 *
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
/* Get a bit mask of the bits set in non-uint32 aligned addresses */
#define UINT32_ALIGN_MASK (sizeof(uint32) - 1)

/* Rotate a uint32 value left by k bits - note multiple evaluation! */
#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))

/*----------
 * mix -- mix 3 32-bit values reversibly.
 *
 * This is reversible, so any information in (a,b,c) before mix() is
 * still in (a,b,c) after mix().
 *
 * If four pairs of (a,b,c) inputs are run through mix(), or through
 * mix() in reverse, there are at least 32 bits of the output that
 * are sometimes the same for one pair and different for another pair.
 * This was tested for:
 * * pairs that differed by one bit, by two bits, in any combination
 *	 of top bits of (a,b,c), or in any combination of bottom bits of
 *	 (a,b,c).
 * * "differ" is defined as +, -, ^, or ~^.  For + and -, I transformed
 *	 the output delta to a Gray code (a^(a>>1)) so a string of 1's (as
 *	 is commonly produced by subtraction) look like a single 1-bit
 *	 difference.
 * * the base values were pseudorandom, all zero but one bit set, or
 *	 all zero plus a counter that starts at zero.
 *
 * This does not achieve avalanche.  There are input bits of (a,b,c)
 * that fail to affect some output bits of (a,b,c), especially of a.  The
 * most thoroughly mixed value is c, but it doesn't really even achieve
 * avalanche in c.
 *
 * This allows some parallelism.  Read-after-writes are good at doubling
 * the number of bits affected, so the goal of mixing pulls in the opposite
 * direction from the goal of parallelism.	I did what I could.  Rotates
 * seem to cost as much as shifts on every machine I could lay my hands on,
 * and rotates are much kinder to the top and bottom bits, so I used rotates.
 *----------
 */
#define mix(a,b,c) \
{ \
  a -= c;  a ^= rot(c, 4);	c += b; \
  b -= a;  b ^= rot(a, 6);	a += c; \
  c -= b;  c ^= rot(b, 8);	b += a; \
  a -= c;  a ^= rot(c,16);	c += b; \
  b -= a;  b ^= rot(a,19);	a += c; \
  c -= b;  c ^= rot(b, 4);	b += a; \
}

/*----------
 * final -- final mixing of 3 32-bit values (a,b,c) into c
 *
 * Pairs of (a,b,c) values differing in only a few bits will usually
 * produce values of c that look totally different.  This was tested for
 * * pairs that differed by one bit, by two bits, in any combination
 *	 of top bits of (a,b,c), or in any combination of bottom bits of
 *	 (a,b,c).
 * * "differ" is defined as +, -, ^, or ~^.  For + and -, I transformed
 *	 the output delta to a Gray code (a^(a>>1)) so a string of 1's (as
 *	 is commonly produced by subtraction) look like a single 1-bit
 *	 difference.
 * * the base values were pseudorandom, all zero but one bit set, or
 *	 all zero plus a counter that starts at zero.
 *
 * The use of separate functions for mix() and final() allow for a
 * substantial performance increase since final() does not need to
 * do well in reverse, but is does need to affect all output bits.
 * mix(), on the other hand, does not need to affect all output
 * bits (affecting 32 bits is enough).	The original hash function had
 * a single mixing operation that had to satisfy both sets of requirements
 * and was slower as a result.
 *----------
 */
#define final(a,b,c) \
{ \
  c ^= b; c -= rot(b,14); \
  a ^= c; a -= rot(c,11); \
  b ^= a; b -= rot(a,25); \
  c ^= b; c -= rot(b,16); \
  a ^= c; a -= rot(c, 4); \
  b ^= a; b -= rot(a,14); \
  c ^= b; c -= rot(b,24); \
}
/*
 * hash_any() -- hash a variable-length key into a 32-bit value
 *		k		: the key (the unaligned variable-length array of bytes)
 *		len		: the length of the key, counting by bytes
 *
 * Returns a uint32 value.	Every bit of the key affects every bit of
 * the return value.  Every 1-bit and 2-bit delta achieves avalanche.
 * About 6*len+35 instructions. The best hash table sizes are powers
 * of 2.  There is no need to do mod a prime (mod is sooo slow!).
 * If you need less than 32 bits, use a bitmask.
 *
 * Note: we could easily change this function to return a 64-bit hash value
 * by using the final values of both b and c.  b is perhaps a little less
 * well mixed than c, however.
 */
uint32
hash_any(register const unsigned char *k, register int keylen)
{
	register uint32 a,
				b,
				c,
				len;

	/* Set up the internal state */
	len = keylen;
	a = b = c = 0x9e3779b9 + len + 3923095;

	/* If the source pointer is word-aligned, we use word-wide fetches */
	if (((intptr_t) k & UINT32_ALIGN_MASK) == 0)
	{
		/* Code path for aligned source data */
		register const uint32 *ka = (const uint32 *) k;

		/* handle most of the key */
		while (len >= 12)
		{
			a += ka[0];
			b += ka[1];
			c += ka[2];
			mix(a, b, c);
			ka += 3;
			len -= 12;
		}

		/* handle the last 11 bytes */
		k = (const unsigned char *) ka;
#ifdef WORDS_BIGENDIAN
		switch (len)
		{
			case 11:
				c += ((uint32) k[10] << 8);
				/* fall through */
			case 10:
				c += ((uint32) k[9] << 16);
				/* fall through */
			case 9:
				c += ((uint32) k[8] << 24);
				/* the lowest byte of c is reserved for the length */
				/* fall through */
			case 8:
				b += ka[1];
				a += ka[0];
				break;
			case 7:
				b += ((uint32) k[6] << 8);
				/* fall through */
			case 6:
				b += ((uint32) k[5] << 16);
				/* fall through */
			case 5:
				b += ((uint32) k[4] << 24);
				/* fall through */
			case 4:
				a += ka[0];
				break;
			case 3:
				a += ((uint32) k[2] << 8);
				/* fall through */
			case 2:
				a += ((uint32) k[1] << 16);
				/* fall through */
			case 1:
				a += ((uint32) k[0] << 24);
				/* case 0: nothing left to add */
		}
#else							/* !WORDS_BIGENDIAN */
		switch (len)
		{
			case 11:
				c += ((uint32) k[10] << 24);
				/* fall through */
			case 10:
				c += ((uint32) k[9] << 16);
				/* fall through */
			case 9:
				c += ((uint32) k[8] << 8);
				/* the lowest byte of c is reserved for the length */
				/* fall through */
			case 8:
				b += ka[1];
				a += ka[0];
				break;
			case 7:
				b += ((uint32) k[6] << 16);
				/* fall through */
			case 6:
				b += ((uint32) k[5] << 8);
				/* fall through */
			case 5:
				b += k[4];
				/* fall through */
			case 4:
				a += ka[0];
				break;
			case 3:
				a += ((uint32) k[2] << 16);
				/* fall through */
			case 2:
				a += ((uint32) k[1] << 8);
				/* fall through */
			case 1:
				a += k[0];
				/* case 0: nothing left to add */
		}
#endif   /* WORDS_BIGENDIAN */
	}
	else
	{
		/* Code path for non-aligned source data */

		/* handle most of the key */
		while (len >= 12)
		{
#ifdef WORDS_BIGENDIAN
			a += (k[3] + ((uint32) k[2] << 8) + ((uint32) k[1] << 16) + ((uint32) k[0] << 24));
			b += (k[7] + ((uint32) k[6] << 8) + ((uint32) k[5] << 16) + ((uint32) k[4] << 24));
			c += (k[11] + ((uint32) k[10] << 8) + ((uint32) k[9] << 16) + ((uint32) k[8] << 24));
#else							/* !WORDS_BIGENDIAN */
			a += (k[0] + ((uint32) k[1] << 8) + ((uint32) k[2] << 16) + ((uint32) k[3] << 24));
			b += (k[4] + ((uint32) k[5] << 8) + ((uint32) k[6] << 16) + ((uint32) k[7] << 24));
			c += (k[8] + ((uint32) k[9] << 8) + ((uint32) k[10] << 16) + ((uint32) k[11] << 24));
#endif   /* WORDS_BIGENDIAN */
			mix(a, b, c);
			k += 12;
			len -= 12;
		}

		/* handle the last 11 bytes */
#ifdef WORDS_BIGENDIAN
		switch (len)			/* all the case statements fall through */
		{
			case 11:
				c += ((uint32) k[10] << 8);
			case 10:
				c += ((uint32) k[9] << 16);
			case 9:
				c += ((uint32) k[8] << 24);
				/* the lowest byte of c is reserved for the length */
			case 8:
				b += k[7];
			case 7:
				b += ((uint32) k[6] << 8);
			case 6:
				b += ((uint32) k[5] << 16);
			case 5:
				b += ((uint32) k[4] << 24);
			case 4:
				a += k[3];
			case 3:
				a += ((uint32) k[2] << 8);
			case 2:
				a += ((uint32) k[1] << 16);
			case 1:
				a += ((uint32) k[0] << 24);
				/* case 0: nothing left to add */
		}
#else							/* !WORDS_BIGENDIAN */
		switch (len)			/* all the case statements fall through */
		{
			case 11:
				c += ((uint32) k[10] << 24);
			case 10:
				c += ((uint32) k[9] << 16);
			case 9:
				c += ((uint32) k[8] << 8);
				/* the lowest byte of c is reserved for the length */
			case 8:
				b += ((uint32) k[7] << 24);
			case 7:
				b += ((uint32) k[6] << 16);
			case 6:
				b += ((uint32) k[5] << 8);
			case 5:
				b += k[4];
			case 4:
				a += ((uint32) k[3] << 24);
			case 3:
				a += ((uint32) k[2] << 16);
			case 2:
				a += ((uint32) k[1] << 8);
			case 1:
				a += k[0];
				/* case 0: nothing left to add */
		}
#endif   /* WORDS_BIGENDIAN */
	}

	final(a, b, c);

	/* report the result */
	return UInt32GetDatum(c);
}
