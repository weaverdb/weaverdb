/* ----------
 * lztext.c -
 *
 * $Header: /cvs/weaver/mtpgsql/src/backend/utils/adt/lztext.c,v 1.1.1.1 2006/08/12 00:21:45 synmscott Exp $
 *
 *	Text type with internal LZ compressed representation. Uses the
 *	standard PostgreSQL compression method.
 *
 *	This code requires that the LZ compressor found in pg_lzcompress
 *	codes a usable VARSIZE word at the beginning of the output buffer.
 * ----------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include "postgres.h"

#include "utils/builtins.h"
#include "utils/palloc.h"
#include "utils/pg_lzcompress.h"
#ifdef MULTIBYTE
#include "mb/pg_wchar.h"
#endif

#define UNCOMPRESSED(length) ((length | 0x80000000))
#define ISCOMPRESSED(lz) (!(lz->vl_len_ & 0x80000000))

/* ----------
 * lztextin -
 *
 *		Input function for datatype lztext
 * ----------
 */
lztext *
lztextin(char *str)
{
	lztext	   *result;
	int32		rawsize;
	lztext	   *tmp;
	int			tmp_size;
        int     clen;

	/* ----------
	 * Handle NULL
	 * ----------
	 */
	if (str == NULL)
		return NULL;

	/* ----------
	 * Determine input size and maximum output Datum size
	 * ----------
	 */
	rawsize = strlen(str);
	tmp_size = PGLZ_MAX_OUTPUT(rawsize);

	/* ----------
	 * Allocate a temporary result and compress into it
	 * ----------
	 */
	tmp = (lztext *) palloc(tmp_size);
	clen = pglz_compress(str, rawsize, tmp, NULL);

	/* ----------
	 * If we miss less than 25% bytes at the end of the temp value,
	 * so be it. Therefore we save a palloc()/memcpy()/pfree()
	 * sequence.
	 * ----------
	 */
        if ( clen == 0 ) {
            memmove(VARDATA(tmp),str,rawsize);
            SETVARSIZE(tmp,UNCOMPRESSED(rawsize + VARHDRSZ));
            return tmp;
        } 
        
        
        if (tmp_size - clen < 256 ||
		tmp_size -  clen < tmp_size / 4)
		result = tmp;
	else
	{
		result = (lztext *) palloc(clen);
		memcpy(result, tmp, clen);
		pfree(tmp);
	}

        SETVARSIZE(result,clen);
	return result;
}


/* ----------
 * lztextout -
 *
 *		Output function for data type lztext
 * ----------
 */
char *
lztextout(lztext *lz)
{
	char	   *result;

	/* ----------
	 * Handle NULL
	 * ----------
	 */
	if (lz == NULL)
	{
		result = (char *) palloc(2);
		result[0] = '-';
		result[1] = '\0';
		return result;
	}

	/* ----------
	 * Allocate the result string - the required size is remembered
	 * in the lztext header so we don't need a temporary buffer or
	 * have to diddle with realloc's.
	 * ----------
	 */

	/* ----------
	 * Decompress and add terminating ZERO
	 * ----------
	 */
        if ( !ISCOMPRESSED(lz) )  {
                result = (char *) palloc(VARSIZE(lz) - VARHDRSZ + 1);
                memmove(result,VARDATA(lz),VARSIZE(lz) - VARHDRSZ);
                result[VARSIZE(lz) - VARHDRSZ] = '\0';
        } else {
                result = (char *) palloc(PGLZ_RAW_SIZE(lz) + 1);
                pglz_decompress(lz, result);
        	result[lz->rawsize] = '\0';
        }

	/* ----------
	 * Return the result
	 * ----------
	 */
	return result;
}


/* ----------
 * lztextlen -
 *
 *	Logical length of lztext field (it's the uncompressed size
 *	of the original data).
 * ----------
 */
int32
lztextlen(lztext *lz)
{
	int			l;
#ifdef MULTIBYTE
	unsigned char *s1,
			   *s2;
	int			len;
	int			wl;

#endif
	/* ----------
	 * Handle NULL
	 * ----------
	 */
	if (lz == NULL)
		return 0;

        if ( !ISCOMPRESSED(lz) ) l = VARSIZE(lz) - VARHDRSZ;
        else l = PGLZ_RAW_SIZE(lz);

#ifdef MULTIBYTE
	len = 0;
	s1 = s2 = (unsigned char *) lztextout(lz);
	while (l > 0)
	{
		wl = pg_mblen(s1);
		l -= wl;
		s1 += wl;
		len++;
	}
	pfree((char *) s2);
	return (len);
#else
	/* ----------
	 * without multibyte support, it's the remembered rawsize
	 * ----------
	 */
        return l;
#endif
}


/* ----------
 * lztextoctetlen -
 *
 *	Physical length of lztext field (it's the compressed size
 *	plus the rawsize field).
 * ----------
 */
int32
lztextoctetlen(lztext *lz)
{
	/* ----------
	 * Handle NULL
	 * ----------
	 */
	if (lz == NULL)
		return 0;

	/* ----------
	 * Return the varsize minus the VARSIZE field itself.
	 * ----------
	 */
	return VARSIZE(lz) - VARHDRSZ;
}


/* ----------
 * text_lztext -
 *
 *	Convert text to lztext
 * ----------
 */
lztext *
text_lztext(text *txt)
{
	lztext	   *result;
	int32		rawsize;
	lztext	   *tmp;
	int			tmp_size;
	char	   *str;
        int        clen;

	/* ----------
	 * Handle NULL
	 * ----------
	 */
	if (txt == NULL)
		return NULL;

	/* ----------
	 * Determine input size and eventually tuple size
	 * ----------
	 */
	rawsize = VARSIZE(txt) - VARHDRSZ;
	str = VARDATA(txt);
	tmp_size = PGLZ_MAX_OUTPUT(rawsize);

	/* ----------
	 * Allocate a temporary result and compress into it
	 * ----------
	 */
	tmp = (lztext *) palloc(tmp_size);
	clen = pglz_compress(str, rawsize, tmp, NULL);

	/* ----------
	 * If we miss less than 25% bytes at the end of the temp value,
	 * so be it. Therefore we save a palloc()/memcpy()/pfree()
	 * sequence.
	 * ----------
	 */
        if ( clen == 0 ) {
            memmove(VARDATA(tmp),str,rawsize);
            SETVARSIZE(tmp,UNCOMPRESSED(rawsize + VARHDRSZ));
            return tmp;
        }
        
        if (tmp_size - clen < 256 ||
		tmp_size - clen < tmp_size / 4)
		result = tmp;
	else
	{
		result = (lztext *) palloc(clen);
		memcpy(result, tmp, clen);
		pfree(tmp);
	}

        SETVARSIZE(result,clen);
	return result;
}


/* ----------
 * lztext_text -
 *
 *	Convert lztext to text
 * ----------
 */
text *
lztext_text(lztext *lz)
{
	text	   *result;

	/* ----------
	 * Handle NULL
	 * ----------
	 */
	if (lz == NULL)
		return NULL;

	/* ----------
	 * Decompress directly into the text data area.
	 * ----------
	 */
        if ( !ISCOMPRESSED(lz) ) {
            result = palloc(VARSIZE(lz));
            memmove(VARDATA(result),VARDATA(lz),VARSIZE(lz) - VARHDRSZ);
            SETVARSIZE(result,VARSIZE(lz));
        } else {
            result = palloc(PGLZ_RAW_SIZE(lz) + VARHDRSZ);
            pglz_decompress(lz, VARDATA(result));
            SETVARSIZE(result,PGLZ_RAW_SIZE(lz) + VARHDRSZ);
        }

	return result;
}


/* ----------
 * lztext_cmp -
 *
 *		Comparision function for two lztext datum's.
 *
 *		Returns -1, 0 or 1.
 * ----------
 */
int32
lztext_cmp(lztext *lz1, lztext *lz2)
{

	char	   *cp1;
	char	   *cp2;
	int			result;

	if (lz1 == NULL || lz2 == NULL)
		return (int32) 0;

	cp1 = lztextout(lz1);
	cp2 = lztextout(lz2);

	result = strcoll(cp1, cp2);

	pfree(cp1);
	pfree(cp2);

	return result;
}


/* ----------
 * lztext_eq ... -
 *
 *		=, !=, >, >=, < and <= operator functions for two
 *		lztext datums.
 * ----------
 */
bool
lztext_eq(lztext *lz1, lztext *lz2)
{
	if (lz1 == NULL || lz2 == NULL)
		return false;

	return (bool) (lztext_cmp(lz1, lz2) == 0);
}


bool
lztext_ne(lztext *lz1, lztext *lz2)
{
	if (lz1 == NULL || lz2 == NULL)
		return false;

	return (bool) (lztext_cmp(lz1, lz2) != 0);
}


bool
lztext_gt(lztext *lz1, lztext *lz2)
{
	if (lz1 == NULL || lz2 == NULL)
		return false;

	return (bool) (lztext_cmp(lz1, lz2) > 0);
}


bool
lztext_ge(lztext *lz1, lztext *lz2)
{
	if (lz1 == NULL || lz2 == NULL)
		return false;

	return (bool) (lztext_cmp(lz1, lz2) >= 0);
}


bool
lztext_lt(lztext *lz1, lztext *lz2)
{
	if (lz1 == NULL || lz2 == NULL)
		return false;

	return (bool) (lztext_cmp(lz1, lz2) < 0);
}


bool
lztext_le(lztext *lz1, lztext *lz2)
{
	if (lz1 == NULL || lz2 == NULL)
		return false;

	return (bool) (lztext_cmp(lz1, lz2) <= 0);
}
