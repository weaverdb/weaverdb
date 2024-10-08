/*-------------------------------------------------------------------------
 *
 * arrayfuncs.c
 *	  Special functions for arrays.
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


#include "catalog/catalog.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "libpq/be-fsstubs.h"
#include "libpq/libpq-fs.h"
#include "storage/fd.h"
#include "utils/array.h"
#include "utils/memutils.h"
#include "utils/syscache.h"

#define ASSGN	 "="

/* An array has the following internal structure:
 *	  <nbytes>		- total number of bytes
 *	  <ndim>		- number of dimensions of the array
 *	  <flags>		- bit mask of flags
 *	  <dim>			- size of each array axis
 *	  <dim_lower>	- lower boundary of each dimension
 *	  <actual data> - whatever is the stored data
 */

/*-=-=--=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-*/
static int	_ArrayCount(char *str, int *dim, int typdelim);
static char *_ReadArrayStr(char *arrayStr, int nitems, int ndim, int *dim,
			  FmgrInfo *inputproc, Oid typelem, int32 typmod,
			  char typdelim, int typlen, bool typbyval,
			  char typalign, int *nbytes);

#ifdef LOARRAY
static char *_ReadLOArray(char *str, int *nbytes, int *fd, bool *chunkFlag,
			 int ndim, int *dim, int baseSize);

#endif
static void _CopyArrayEls(char **values, char *p, int nitems, int typlen,
			  char typalign, bool typbyval);
static void system_cache_lookup(Oid element_type, bool input, int *typlen,
				 bool *typbyval, char *typdelim, Oid *typelem, Oid *proc,
					char *typalign);
static Datum _ArrayCast(char *value, bool byval, int len);

#ifdef LOARRAY
static char *_AdvanceBy1word(char *str, char **word);

#endif
static void _ArrayRange(int *st, int *endp, int bsize, char *destPtr,
			ArrayType *array, int from);
static int	_ArrayClipCount(int *stI, int *endpI, ArrayType *array);
static void _LOArrayRange(int *st, int *endp, int bsize, int srcfd,
			  int destfd, ArrayType *array, int isSrcLO, bool *isNull);
static void _ReadArray(int *st, int *endp, int bsize, int srcfd, int destfd,
		   ArrayType *array, int isDestLO, bool *isNull);
static int	ArrayCastAndSet(char *src, bool typbyval, int typlen, char *dest);
static int	SanityCheckInput(int ndim, int n, int *dim, int *lb, int *indx);
static int	array_read(char *destptr, int eltsize, int nitems, char *srcptr);
static char *array_seek(char *ptr, int eltsize, int nitems);

/*---------------------------------------------------------------------
 * array_in :
 *		  converts an array from the external format in "string" to
 *		  it internal format.
 * return value :
 *		  the internal representation of the input array
 *--------------------------------------------------------------------
 */
char *
array_in(char *string,			/* input array in external form */
		 Oid element_type,		/* type OID of an array element */
		 int32 typmod)
{
	int			typlen;
	bool		typbyval,
				done;
	char		typdelim;
	Oid			typinput;
	Oid			typelem;
	char	   *string_save,
			   *p,
			   *q,
			   *r;
	FmgrInfo	inputproc;
	int			i,
				nitems;
	int32		nbytes;
	char	   *dataPtr;
	ArrayType  *retval = NULL;
	int			ndim,
				dim[MAXDIM],
				lBound[MAXDIM];
	char		typalign;

	system_cache_lookup(element_type, true, &typlen, &typbyval, &typdelim,
						&typelem, &typinput, &typalign);

	fmgr_info(typinput, &inputproc);

	string_save = (char *) palloc(strlen(string) + 3);
	strcpy(string_save, string);

	/* --- read array dimensions  ---------- */
	p = q = string_save;
	done = false;
	for (ndim = 0; !done;)
	{
		while (isspace(*p))
			p++;
		if (*p == '[')
		{
			p++;
			if ((r = (char *) strchr(p, ':')) == (char *) NULL)
				lBound[ndim] = 1;
			else
			{
				*r = '\0';
				lBound[ndim] = atoi(p);
				p = r + 1;
			}
			for (q = p; isdigit(*q); q++);
			if (*q != ']')
				elog(ERROR, "array_in: missing ']' in array declaration");
			*q = '\0';
			dim[ndim] = atoi(p);
			if ((dim[ndim] < 0) || (lBound[ndim] < 0))
				elog(ERROR, "array_in: array dimensions need to be positive");
			dim[ndim] = dim[ndim] - lBound[ndim] + 1;
			if (dim[ndim] < 0)
				elog(ERROR, "array_in: upper_bound cannot be < lower_bound");
			p = q + 1;
			ndim++;
		}
		else
			done = true;
	}

	if (ndim == 0)
	{
		if (*p == '{')
		{
			ndim = _ArrayCount(p, dim, typdelim);
			for (i = 0; i < ndim; lBound[i++] = 1);
		}
		else
			elog(ERROR, "array_in: Need to specify dimension");
	}
	else
	{
		while (isspace(*p))
			p++;
		if (strncmp(p, ASSGN, strlen(ASSGN)))
			elog(ERROR, "array_in: missing assignment operator");
		p += strlen(ASSGN);
		while (isspace(*p))
			p++;
	}

#ifdef ARRAYDEBUG
	printf("array_in- ndim %d (", ndim);
	for (i = 0; i < ndim; i++)
	{
		printf(" %d", dim[i]);
	};
	printf(") for %s\n", string);
#endif

	nitems = getNitems(ndim, dim);
	if (nitems == 0)
	{
		char	   *emptyArray = palloc(sizeof(ArrayType));

		MemSet(emptyArray, 0, sizeof(ArrayType));
		*(int32 *) emptyArray = sizeof(ArrayType);
		return emptyArray;
	}

	if (*p == '{')
	{
		/* array not a large object */
		dataPtr = (char *) _ReadArrayStr(p, nitems, ndim, dim, &inputproc, typelem,
							typmod, typdelim, typlen, typbyval, typalign,
										 &nbytes);
		nbytes += ARR_OVERHEAD(ndim);
		retval = (ArrayType *) palloc(nbytes);
		MemSet(retval, 0, nbytes);
		memmove(retval, (char *) &nbytes, sizeof(int));
		memmove((char *) ARR_NDIM_PTR(retval), (char *) &ndim, sizeof(int));
		SET_LO_FLAG(false, retval);
		memmove((char *) ARR_DIMS(retval), (char *) dim, ndim * sizeof(int));
		memmove((char *) ARR_LBOUND(retval), (char *) lBound,
				ndim * sizeof(int));

		/*
		 * dataPtr is an array of arbitraystuff even though its type is
		 * char* cast to char** to pass to _CopyArrayEls for now  - jolly
		 */
		_CopyArrayEls((char **) dataPtr,
					  ARR_DATA_PTR(retval), nitems,
					  typlen, typalign, typbyval);
	}
	else
	{
#ifdef LOARRAY
		int			dummy,
					bytes;
		bool		chunked = false;

		dataPtr = _ReadLOArray(p, &bytes, &dummy, &chunked, ndim,
							   dim, typlen);
		nbytes = bytes + ARR_OVERHEAD(ndim);
		retval = (ArrayType *) palloc(nbytes);
		MemSet(retval, 0, nbytes);
		memmove(retval, (char *) &nbytes, sizeof(int));
		memmove((char *) ARR_NDIM_PTR(retval), (char *) &ndim, sizeof(int));
		SET_LO_FLAG(true, retval);
		SET_CHUNK_FLAG(chunked, retval);
		memmove((char *) ARR_DIMS(retval), (char *) dim, ndim * sizeof(int));
		memmove((char *) ARR_LBOUND(retval), (char *) lBound, ndim * sizeof(int));
		memmove(ARR_DATA_PTR(retval), dataPtr, bytes);
#endif
		elog(ERROR, "large object arrays not supported");
	}
	pfree(string_save);
	return (char *) retval;
}

/*-----------------------------------------------------------------------------
 * _ArrayCount
 *	 Counts the number of dimensions and the *dim array for an array string.
 *		 The syntax for array input is C-like nested curly braces
 *-----------------------------------------------------------------------------
 */
static int
_ArrayCount(char *str, int *dim, int typdelim)
{
	int			nest_level = 0,
				i;
	int			ndim = 0,
				temp[MAXDIM];
	bool		scanning_string = false;
	bool		eoArray = false;
	char	   *q;

	for (i = 0; i < MAXDIM; ++i)
		temp[i] = dim[i] = 0;

	if (strncmp(str, "{}", 2) == 0)
		return 0;

	q = str;
	while (eoArray != true)
	{
		bool		done = false;

		while (!done)
		{
			switch (*q)
			{
				case '\\':
					/* skip escaped characters (\ and ") inside strings */
					if (scanning_string && *(q + 1))
						q++;
					break;
				case '\0':

					/*
					 * Signal a premature end of the string.  DZ -
					 * 2-9-1996
					 */
					elog(ERROR, "malformed array constant: %s", str);
					break;
				case '\"':
					scanning_string = !scanning_string;
					break;
				case '{':
					if (!scanning_string)
					{
						temp[nest_level] = 0;
						nest_level++;
					}
					break;
				case '}':
					if (!scanning_string)
					{
						if (!ndim)
							ndim = nest_level;
						nest_level--;
						if (nest_level)
							temp[nest_level - 1]++;
						if (nest_level == 0)
							eoArray = done = true;
					}
					break;
				default:
					if (!ndim)
						ndim = nest_level;
					if (*q == typdelim && !scanning_string)
						done = true;
					break;
			}
			if (!done)
				q++;
		}
		temp[ndim - 1]++;
		q++;
		if (!eoArray)
			while (isspace(*q))
				q++;
	}
	for (i = 0; i < ndim; ++i)
		dim[i] = temp[i];

	return ndim;
}

/*---------------------------------------------------------------------------
 * _ReadArrayStr :
 *	 parses the array string pointed by "arrayStr" and converts it in the
 *	 internal format. The external format expected is like C array
 *	 declaration. Unspecified elements are initialized to zero for fixed length
 *	 base types and to empty varlena structures for variable length base
 *	 types.
 * result :
 *	 returns the internal representation of the array elements
 *	 nbytes is set to the size of the array in its internal representation.
 *---------------------------------------------------------------------------
 */
static char *
_ReadArrayStr(char *arrayStr,
			  int nitems,
			  int ndim,
			  int *dim,
			  FmgrInfo *inputproc,		/* function used for the
										 * conversion */
			  Oid typelem,
			  int32 typmod,
			  char typdelim,
			  int typlen,
			  bool typbyval,
			  char typalign,
			  int *nbytes)
{
	int			i,
				nest_level = 0;
	char	   *p,
			   *q,
			   *r,
			  **values;
	bool		scanning_string = false;
	int			indx[MAXDIM],
				prod[MAXDIM];
	bool		eoArray = false;

	mda_get_prod(ndim, dim, prod);
	for (i = 0; i < ndim; indx[i++] = 0);
	/* read array enclosed within {} */
	values = (char **) palloc(nitems * sizeof(char *));
	MemSet(values, 0, nitems * sizeof(char *));
	q = p = arrayStr;

	while (!eoArray)
	{
		bool		done = false;
		int			i = -1;

		while (!done)
		{
			switch (*q)
			{
				case '\\':
					/* Crunch the string on top of the backslash. */
					for (r = q; *r != '\0'; r++)
						*r = *(r + 1);
					break;
				case '\"':
					if (!scanning_string)
					{
						while (p != q)
							p++;
						p++;	/* get p past first doublequote */
					}
					else
						*q = '\0';
					scanning_string = !scanning_string;
					break;
				case '{':
					if (!scanning_string)
					{
						p++;
						nest_level++;
						if (nest_level > ndim)
							elog(ERROR, "array_in: illformed array constant");
						indx[nest_level - 1] = 0;
						indx[ndim - 1] = 0;
					}
					break;
				case '}':
					if (!scanning_string)
					{
						if (i == -1)
							i = tuple2linear(ndim, indx, prod);
						nest_level--;
						if (nest_level == 0)
							eoArray = done = true;
						else
						{
							*q = '\0';
							indx[nest_level - 1]++;
						}
					}
					break;
				default:
					if (*q == typdelim && !scanning_string)
					{
						if (i == -1)
							i = tuple2linear(ndim, indx, prod);
						done = true;
						indx[ndim - 1]++;
					}
					break;
			}
			if (!done)
				q++;
		}
		*q = '\0';
		if (i >= nitems)
			elog(ERROR, "array_in: illformed array constant");
		values[i] = (*fmgr_faddr(inputproc)) (p, typelem, typmod);
		p = ++q;
		if (!eoArray)

			/*
			 * if not at the end of the array skip white space
			 */
			while (isspace(*q))
			{
				p++;
				q++;
			}
	}
	if (typlen > 0)
	{
		*nbytes = nitems * typlen;
		if (!typbyval)
			for (i = 0; i < nitems; i++)
				if (!values[i])
				{
					values[i] = palloc(typlen);
					MemSet(values[i], 0, typlen);
				}
	}
	else
	{
		for (i = 0, *nbytes = 0; i < nitems; i++)
		{
			if (values[i])
			{
				if (typalign == 'd')
					*nbytes += MAXALIGN(*(int32 *) values[i]);
				else
					*nbytes += INTALIGN(*(int32 *) values[i]);
			}
			else
			{
				*nbytes += sizeof(int32);
				values[i] = palloc(sizeof(int32));
				*(int32 *) values[i] = sizeof(int32);
			}
		}
	}
	return (char *) values;
}


/*----------------------------------------------------------------------------
 * Read data about an array to be stored as a large object
 *----------------------------------------------------------------------------
 */
#ifdef LOARRAY
static char *
_ReadLOArray(char *str,
			 int *nbytes,
			 int *fd,
			 bool *chunkFlag,
			 int ndim,
			 int *dim,
			 int baseSize)
{
	char	   *inputfile,
			   *accessfile = NULL,
			   *chunkfile = NULL;
	char	   *retStr,
			   *_AdvanceBy1word();
	Oid			lobjId;

	str = _AdvanceBy1word(str, &inputfile);

	while (str != NULL)
	{
		char	   *word;

		str = _AdvanceBy1word(str, &word);

		if (!strcmp(word, "-chunk"))
		{
			if (str == NULL)
				elog(ERROR, "array_in: access pattern file required");
			str = _AdvanceBy1word(str, &accessfile);
		}
		else if (!strcmp(word, "-noreorg"))
		{
			if (str == NULL)
				elog(ERROR, "array_in: chunk file required");
			str = _AdvanceBy1word(str, &chunkfile);
		}
		else
			elog(ERROR, "usage: <input file> -chunk DEFAULT/<access pattern file> -invert/-native [-noreorg <chunk file>]");
	}

	if (inputfile == NULL)
		elog(ERROR, "array_in: missing file name");
	lobjId = lo_creat(0);
	*fd = lo_open(lobjId, INV_READ);
	if (*fd < 0)
		elog(ERROR, "Large object create failed");
	retStr = inputfile;
	*nbytes = strlen(retStr) + 2;

	if (accessfile)
	{
		FILE	   *afd;

#ifndef __CYGWIN32__
		if ((afd = AllocateFile(accessfile, "r")) == NULL)
#else
		if ((afd = AllocateFile(accessfile, "r")) == NULL)
#endif
			elog(ERROR, "unable to open access pattern file");
		*chunkFlag = true;
		retStr = _ChunkArray(*fd, afd, ndim, dim, baseSize, nbytes,
							 chunkfile);
		FreeFile(afd);
	}
	return retStr;
}

#endif

static void
_CopyArrayEls(char **values,
			  char *p,
			  int nitems,
			  int typlen,
			  char typalign,
			  bool typbyval)
{
	int			i;

	for (i = 0; i < nitems; i++)
	{
		int			inc;

		inc = ArrayCastAndSet(values[i], typbyval, typlen, p);
		p += inc;
		if (!typbyval)
			pfree(values[i]);
	}
	pfree(values);
}

/*-------------------------------------------------------------------------
 * array_out :
 *		   takes the internal representation of an array and returns a string
 *		  containing the array in its external format.
 *-------------------------------------------------------------------------
 */
char *
array_out(ArrayType *v, Oid element_type)
{
	int			typlen;
	bool		typbyval;
	char		typdelim;
	Oid			typoutput,
				typelem;
	FmgrInfo	outputproc;
	char		typalign;

	char	   *p,
			   *tmp,
			   *retval,
			  **values,
				delim[2];
	int			nitems,
				overall_length,
				i,
				j,
				k,
#ifndef TCL_ARRAYS
				l,
#endif
				indx[MAXDIM];
	bool		dummy_bool;
	int			ndim,
			   *dim;

	if (v == (ArrayType *) NULL)
		return (char *) NULL;

	if (ARR_IS_LO(v) == true)
	{
		char	   *p,
				   *save_p;
		int			nbytes;

		/* get a wide string to print to */
		p = array_dims(v, &dummy_bool);
		nbytes = strlen(ARR_DATA_PTR(v)) + VARHDRSZ + *(int *) p;

		save_p = (char *) palloc(nbytes);

		strcpy(save_p, p + sizeof(int));
		strcat(save_p, ASSGN);
		strcat(save_p, ARR_DATA_PTR(v));
		pfree(p);
		return save_p;
	}

	system_cache_lookup(element_type, false, &typlen, &typbyval,
						&typdelim, &typelem, &typoutput, &typalign);
	fmgr_info(typoutput, &outputproc);
	sprintf(delim, "%c", typdelim);
	ndim = ARR_NDIM(v);
	dim = ARR_DIMS(v);
	nitems = getNitems(ndim, dim);

	if (nitems == 0)
	{
		char	   *emptyArray = palloc(3);

		emptyArray[0] = '{';
		emptyArray[1] = '}';
		emptyArray[2] = '\0';
		return emptyArray;
	}

	p = ARR_DATA_PTR(v);
	overall_length = 1;			/* [TRH] don't forget to count \0 at end. */
	values = (char **) palloc(nitems * sizeof(char *));
	for (i = 0; i < nitems; i++)
	{
		if (typbyval)
		{
			switch (typlen)
			{
				case 1:
					values[i] = (*fmgr_faddr(&outputproc)) (*p, typelem, -1);
					break;
				case 2:
					values[i] = (*fmgr_faddr(&outputproc)) (*(int16 *) p, typelem, -1);
					break;
				case 3:
				case 4:
					values[i] = (*fmgr_faddr(&outputproc)) (*(int32 *) p, typelem, -1);
					break;
			}
			p += typlen;
		}
		else
		{
			values[i] = (*fmgr_faddr(&outputproc)) (p, typelem, -1);
			if (typlen > 0)
				p += typlen;
			else
				p += INTALIGN(*(int32 *) p);

			/*
			 * For the pair of double quotes
			 */
			overall_length += 2;
		}
		for (tmp = values[i]; *tmp; tmp++)
		{
			overall_length += 1;
#ifndef TCL_ARRAYS
			if (*tmp == '"')
				overall_length += 1;
#endif
		}
		overall_length += 1;
	}

	/*
	 * count total number of curly braces in output string
	 */
	for (i = j = 0, k = 1; i < ndim; k *= dim[i++], j += k);

	p = (char *) palloc(overall_length + 2 * j);
	retval = p;

	strcpy(p, "{");
	for (i = 0; i < ndim; indx[i++] = 0);
	j = 0;
	k = 0;
	do
	{
		for (i = j; i < ndim - 1; i++)
			strcat(p, "{");

		/*
		 * Surround anything that is not passed by value in double quotes.
		 * See above for more details.
		 */
		if (!typbyval)
		{
			strcat(p, "\"");
#ifndef TCL_ARRAYS
			l = strlen(p);
			for (tmp = values[k]; *tmp; tmp++)
			{
				if (*tmp == '"')
					p[l++] = '\\';
				p[l++] = *tmp;
			}
			p[l] = '\0';
#else
			strcat(p, values[k]);
#endif
			strcat(p, "\"");
		}
		else
			strcat(p, values[k]);
		pfree(values[k++]);

		for (i = ndim - 1; i >= 0; i--)
		{
			indx[i] = (indx[i] + 1) % dim[i];
			if (indx[i])
			{
				strcat(p, delim);
				break;
			}
			else
				strcat(p, "}");
		}
		j = i;
	} while (j != -1);

	pfree(values);
	return retval;
}

/*-----------------------------------------------------------------------------
 * array_dims :
 *		  returns the dimension of the array pointed to by "v"
 *----------------------------------------------------------------------------
 */
char *
array_dims(ArrayType *v, bool *isNull)
{
	char	   *p,
			   *save_p;
	int			nbytes,
				i;
	int		   *dimv,
			   *lb;

	if (v == (ArrayType *) NULL)
		RETURN_NULL;
	nbytes = ARR_NDIM(v) * 33;

	/*
	 * 33 since we assume 15 digits per number + ':' +'[]'
	 */
	save_p = p = (char *) palloc(nbytes + VARHDRSZ);
	MemSet(save_p, 0, nbytes + VARHDRSZ);
	dimv = ARR_DIMS(v);
	lb = ARR_LBOUND(v);
	p += VARHDRSZ;
	for (i = 0; i < ARR_NDIM(v); i++)
	{
		sprintf(p, "[%d:%d]", lb[i], dimv[i] + lb[i] - 1);
		p += strlen(p);
	}
	nbytes = strlen(save_p + VARHDRSZ) + VARHDRSZ;
	memmove(save_p, &nbytes, VARHDRSZ);
	return save_p;
}

/*---------------------------------------------------------------------------
 * array_ref :
 *	  This routing takes an array pointer and an index array and returns
 *	  a pointer to the referred element if element is passed by
 *	  reference otherwise returns the value of the referred element.
 *---------------------------------------------------------------------------
 */
Datum
array_ref(ArrayType *array,
		  int n,
		  int *indx,
		  int reftype,
		  int elmlen,
		  int arraylen,
		  bool *isNull)
{
	int			i,
				ndim,
			   *dim,
			   *lb,
				offset,
				nbytes;
	struct varlena *v = NULL;
	char	   *retval = NULL;

	if (array == (ArrayType *) NULL)
		RETURN_NULL;
	if (arraylen > 0)
	{

		/*
		 * fixed length arrays -- these are assumed to be 1-d
		 */
		if (indx[0] * elmlen > arraylen)
			elog(ERROR, "array_ref: array bound exceeded");
		retval = (char *) array + indx[0] * elmlen;
		return _ArrayCast(retval, reftype, elmlen);
	}
	dim = ARR_DIMS(array);
	lb = ARR_LBOUND(array);
	ndim = ARR_NDIM(array);
	nbytes = (*(int32 *) array) - ARR_OVERHEAD(ndim);

	if (!SanityCheckInput(ndim, n, dim, lb, indx))
		RETURN_NULL;

	offset = GetOffset(n, dim, lb, indx);

	if (ARR_IS_LO(array))
	{
		char	   *lo_name;
		int			fd = 0;

		/* We are assuming fixed element lengths here */
		offset *= elmlen;
		lo_name = (char *) ARR_DATA_PTR(array);
#ifdef LOARRAY
		if ((fd = LOopen(lo_name, ARR_IS_INV(array) ? INV_READ : O_RDONLY)) < 0)
			RETURN_NULL;
#endif
		if (ARR_IS_CHUNKED(array))
			v = _ReadChunkArray1El(indx, elmlen, fd, array, isNull);
		else
		{
			if (lo_lseek(fd, offset, SEEK_SET) < 0)
				RETURN_NULL;
#ifdef LOARRAY
			v = (struct varlena *) LOread(fd, elmlen);
#endif
		}
		if (*isNull)
			RETURN_NULL;
		if (VARSIZE(v) - VARHDRSZ < elmlen)
			RETURN_NULL;
		lo_close(fd);
		retval = (char *) _ArrayCast((char *) VARDATA(v), reftype, elmlen);
		if (reftype == 0)
		{						/* not by value */
			char	   *tempdata = palloc(elmlen);

			memmove(tempdata, retval, elmlen);
			retval = tempdata;
		}
		pfree(v);
		return (Datum) retval;
	}

	if (elmlen > 0)
	{
		offset = offset * elmlen;
		/* off the end of the array */
		if (nbytes - offset < 1)
			RETURN_NULL;
		retval = ARR_DATA_PTR(array) + offset;
		return _ArrayCast(retval, reftype, elmlen);
	}
	else
	{
		bool		done = false;
		char	   *temp;
		int			bytes = nbytes;

		temp = ARR_DATA_PTR(array);
		i = 0;
		while (bytes > 0 && !done)
		{
			if (i == offset)
			{
				retval = temp;
				done = true;
			}
			bytes -= INTALIGN(*(int32 *) temp);
			temp += INTALIGN(*(int32 *) temp);
			i++;
		}
		if (!done)
			RETURN_NULL;
		return (Datum) retval;
	}
}

/*-----------------------------------------------------------------------------
 * array_clip :
 *		  This routine takes an array and a range of indices (upperIndex and
 *		   lowerIndx), creates a new array structure for the referred elements
 *		   and returns a pointer to it.
 *-----------------------------------------------------------------------------
 */
Datum
array_clip(ArrayType *array,
		   int n,
		   int *upperIndx,
		   int *lowerIndx,
		   int reftype,
		   int len,
		   bool *isNull)
{
	int			i,
				ndim,
			   *dim,
			   *lb,
				nbytes;
	ArrayType  *newArr;
	int			bytes,
				span[MAXDIM];

	/* timer_start(); */
	if (array == (ArrayType *) NULL)
		RETURN_NULL;
	dim = ARR_DIMS(array);
	lb = ARR_LBOUND(array);
	ndim = ARR_NDIM(array);
	nbytes = (*(int32 *) array) - ARR_OVERHEAD(ndim);

	if (!SanityCheckInput(ndim, n, dim, lb, upperIndx))
		RETURN_NULL;

	if (!SanityCheckInput(ndim, n, dim, lb, lowerIndx))
		RETURN_NULL;

	for (i = 0; i < n; i++)
		if (lowerIndx[i] > upperIndx[i])
			elog(ERROR, "lowerIndex cannot be larger than upperIndx");
	mda_get_range(n, span, lowerIndx, upperIndx);

	if (ARR_IS_LO(array))
	{
#ifdef LOARRAY
		char	   *lo_name;

#endif
		char	   *newname = NULL;
		int			fd = 0,
					newfd = 0,
					isDestLO = true,
					rsize;

		if (len < 0)
			elog(ERROR, "array_clip: array of variable length objects not supported");
#ifdef LOARRAY
		lo_name = (char *) ARR_DATA_PTR(array);
		if ((fd = LOopen(lo_name, ARR_IS_INV(array) ? INV_READ : O_RDONLY)) < 0)
			RETURN_NULL;
		newname = _array_newLO(&newfd, Unix);
#endif
		bytes = strlen(newname) + 1 + ARR_OVERHEAD(n);
		newArr = (ArrayType *) palloc(bytes);
		memmove(newArr, array, sizeof(ArrayType));
		memmove(newArr, &bytes, sizeof(int));
		memmove(ARR_DIMS(newArr), span, n * sizeof(int));
		memmove(ARR_LBOUND(newArr), lowerIndx, n * sizeof(int));
		strcpy(ARR_DATA_PTR(newArr), newname);

		rsize = compute_size(lowerIndx, upperIndx, n, len);
		if (rsize < MAX_BUFF_SIZE)
		{
			char	   *buff;

			rsize += VARHDRSZ;
			buff = palloc(rsize);
			if (buff)
				isDestLO = false;
			if (ARR_IS_CHUNKED(array))
			{
				_ReadChunkArray(lowerIndx, upperIndx, len, fd, &(buff[VARHDRSZ]),
								array, 0, isNull);
			}
			else
			{
				_ReadArray(lowerIndx, upperIndx, len, fd, (long) &(buff[VARHDRSZ]),
						   array,
						   0, isNull);
			}
			memmove(buff, &rsize, VARHDRSZ);
#ifdef LOARRAY
			if (!*isNull)
				bytes = LOwrite(newfd, (struct varlena *) buff);
#endif
			pfree(buff);
		}
		if (isDestLO)
		{
			if (ARR_IS_CHUNKED(array))
			{
				_ReadChunkArray(lowerIndx, upperIndx, len, fd, (char *)(long)newfd, array,
								1, isNull);
			}
			else
				_ReadArray(lowerIndx, upperIndx, len, fd, newfd, array, 1, isNull);
		}
#ifdef LOARRAY
		LOclose(fd);
		LOclose(newfd);
#endif
		if (*isNull)
		{
			pfree(newArr);
			newArr = NULL;
		}
		/* timer_end(); */
		return (Datum) newArr;
	}

	if (len > 0)
	{
		bytes = getNitems(n, span);
		bytes = bytes * len + ARR_OVERHEAD(n);
	}
	else
	{
		bytes = _ArrayClipCount(lowerIndx, upperIndx, array);
		bytes += ARR_OVERHEAD(n);
	}
	newArr = (ArrayType *) palloc(bytes);
	memmove(newArr, array, sizeof(ArrayType));
	memmove(newArr, &bytes, sizeof(int));
	memmove(ARR_DIMS(newArr), span, n * sizeof(int));
	memmove(ARR_LBOUND(newArr), lowerIndx, n * sizeof(int));
	_ArrayRange(lowerIndx, upperIndx, len, ARR_DATA_PTR(newArr), array, 1);
	return (Datum) newArr;
}

/*-----------------------------------------------------------------------------
 * array_set  :
 *		  This routine sets the value of an array location (specified by an index array)
 *		  to a new value specified by "dataPtr".
 * result :
 *		  returns a pointer to the modified array.
 *-----------------------------------------------------------------------------
 */
char *
array_set(ArrayType *array,
		  int n,
		  int *indx,
		  char *dataPtr,
		  int reftype,
		  int elmlen,
		  int arraylen,
		  bool *isNull)
{
	int			ndim,
			   *dim,
			   *lb,
				offset,
				nbytes;
	char	   *pos;

	if (array == (ArrayType *) NULL)
		RETURN_NULL;
	if (arraylen > 0)
	{

		/*
		 * fixed length arrays -- these are assumed to be 1-d
		 */
		if (indx[0] * elmlen > arraylen)
			elog(ERROR, "array_ref: array bound exceeded");
		pos = (char *) array + indx[0] * elmlen;
		ArrayCastAndSet(dataPtr, (bool) reftype, elmlen, pos);
		return (char *) array;
	}
	dim = ARR_DIMS(array);
	lb = ARR_LBOUND(array);
	ndim = ARR_NDIM(array);
	nbytes = (*(int32 *) array) - ARR_OVERHEAD(ndim);

	if (!SanityCheckInput(ndim, n, dim, lb, indx))
	{
		elog(ERROR, "array_set: array bound exceeded");
		return (char *) array;
	}
	offset = GetOffset(n, dim, lb, indx);

	if (ARR_IS_LO(array))
	{
		int			fd = 0;
		struct varlena *v;

		/* We are assuming fixed element lengths here */
		offset *= elmlen;
#ifdef LOARRAY
		char	   *lo_name;

		lo_name = ARR_DATA_PTR(array);
		if ((fd = LOopen(lo_name, ARR_IS_INV(array) ? INV_WRITE : O_WRONLY)) < 0)
			return (char *) array;
#endif
		if (lo_lseek(fd, offset, SEEK_SET) < 0)
			return (char *) array;
		v = (struct varlena *) palloc(elmlen + VARHDRSZ);
		VARSIZE(v) = elmlen + VARHDRSZ;
		ArrayCastAndSet(dataPtr, (bool) reftype, elmlen, VARDATA(v));
#ifdef LOARRAY
		n = LOwrite(fd, v);
#endif

		/*
		 * if (n < VARSIZE(v) - VARHDRSZ) RETURN_NULL;
		 */
		pfree(v);
		lo_close(fd);
		return (char *) array;
	}
	if (elmlen > 0)
	{
		offset = offset * elmlen;
		/* off the end of the array */
		if (nbytes - offset < 1)
			return (char *) array;
		pos = ARR_DATA_PTR(array) + offset;
	}
	else
	{
		ArrayType  *newarray;
		char	   *elt_ptr;
		int			oldsize,
					newsize,
					oldlen,
					newlen,
					lth0,
					lth1,
					lth2;

		elt_ptr = array_seek(ARR_DATA_PTR(array), -1, offset);
		oldlen = INTALIGN(*(int32 *) elt_ptr);
		newlen = INTALIGN(*(int32 *) dataPtr);

		if (oldlen == newlen)
		{
			/* new element with same size, overwrite old data */
			ArrayCastAndSet(dataPtr, (bool) reftype, elmlen, elt_ptr);
			return (char *) array;
		}

		/* new element with different size, reallocate the array */
		oldsize = array->size;
		lth0 = ARR_OVERHEAD(n);
		lth1 = (int) (elt_ptr - ARR_DATA_PTR(array));
		lth2 = (int) (oldsize - lth0 - lth1 - oldlen);
		newsize = lth0 + lth1 + newlen + lth2;

		newarray = (ArrayType *) palloc(newsize);
		memmove((char *) newarray, (char *) array, lth0 + lth1);
		newarray->size = newsize;
		newlen = ArrayCastAndSet(dataPtr, (bool) reftype, elmlen,
								 (char *) newarray + lth0 + lth1);
		memmove((char *) newarray + lth0 + lth1 + newlen,
				(char *) array + lth0 + lth1 + oldlen, lth2);

		/* ??? who should free this storage ??? */
		return (char *) newarray;
	}
	ArrayCastAndSet(dataPtr, (bool) reftype, elmlen, pos);
	return (char *) array;
}

/*----------------------------------------------------------------------------
 * array_assgn :
 *		  This routine sets the value of a range of array locations (specified
 *		  by upper and lower index values ) to new values passed as
 *		  another array
 * result :
 *		  returns a pointer to the modified array.
 *----------------------------------------------------------------------------
 */
char *
array_assgn(ArrayType *array,
			int n,
			int *upperIndx,
			int *lowerIndx,
			ArrayType *newArr,
			int reftype,
			int len,
			bool *isNull)
{
	int			i,
				ndim,
			   *dim,
			   *lb;

	if (array == (ArrayType *) NULL)
		RETURN_NULL;
	if (len < 0)
		elog(ERROR, "array_assgn:updates on arrays of variable length elements not allowed");

	dim = ARR_DIMS(array);
	lb = ARR_LBOUND(array);
	ndim = ARR_NDIM(array);

	if (!SanityCheckInput(ndim, n, dim, lb, upperIndx) ||
		!SanityCheckInput(ndim, n, dim, lb, lowerIndx))
		return (char *) array;

	for (i = 0; i < n; i++)
		if (lowerIndx[i] > upperIndx[i])
			elog(ERROR, "lowerIndex larger than upperIndx");

	if (ARR_IS_LO(array))
	{
		int			fd = 0,
					newfd = 0;

#ifdef LOARRAY
		char	   *lo_name;

		lo_name = (char *) ARR_DATA_PTR(array);
		if ((fd = LOopen(lo_name, ARR_IS_INV(array) ? INV_WRITE : O_WRONLY)) < 0)
			return (char *) array;
#endif
		if (ARR_IS_LO(newArr))
		{
#ifdef LOARRAY
			lo_name = (char *) ARR_DATA_PTR(newArr);
			if ((newfd = LOopen(lo_name, ARR_IS_INV(newArr) ? INV_READ : O_RDONLY)) < 0)
				return (char *) array;
#endif
			_LOArrayRange(lowerIndx, upperIndx, len, fd, newfd, array, 1, isNull);
			lo_close(newfd);
		}
		else
		{
			_LOArrayRange(lowerIndx, upperIndx, len, fd, (long) ARR_DATA_PTR(newArr),
						  array, 0, isNull);
		}
		lo_close(fd);
		return (char *) array;
	}
	_ArrayRange(lowerIndx, upperIndx, len, ARR_DATA_PTR(newArr), array, 0);
	return (char *) array;
}

/*
 * array_map()
 *
 * Map an arbitrary function to an array and return a new array with
 * same dimensions and the source elements transformed by fn().
 */
ArrayType  *
array_map(ArrayType *v,
		  Oid type,
		  char *(*fn) (),
		  Oid retType,
		  int nargs,
		  ...)
{
	ArrayType  *result;
	void	   *args[4];
	char	  **values;
	char	   *elt;
	int		   *dim;
	int			ndim;
	int			nitems;
	int			i;
	int			nbytes = 0;
	int			inp_typlen;
	bool		inp_typbyval;
	int			typlen;
	bool		typbyval;
	char		typdelim;
	Oid			typelem;
	Oid			proc;
	char		typalign;
	char	   *s;
	char	   *p;
	va_list		ap;

	/* Need to guard against NULL input array */
	if (v == NULL)
		return NULL;

	/* Large objects not yet supported */
	if (ARR_IS_LO(v) == true)
		elog(ERROR, "array_map: large objects not supported");

	/* Check nargs */
	if ((nargs < 0) || (nargs > 4))
		elog(ERROR, "array_map: invalid nargs: %d", nargs);

	/* Copy extra args to local variable */
	va_start(ap, nargs);
	for (i = 0; i < nargs; i++)
		args[i] = (void *) va_arg(ap, char *);
	va_end(ap);

	/* Lookup source and result types. Unneeded variables are reused. */
	system_cache_lookup(type, false, &inp_typlen, &inp_typbyval,
						&typdelim, &typelem, &proc, &typalign);
	system_cache_lookup(retType, false, &typlen, &typbyval,
						&typdelim, &typelem, &proc, &typalign);

	ndim = ARR_NDIM(v);
	dim = ARR_DIMS(v);
	nitems = getNitems(ndim, dim);

	/* Check for empty array */
	if (nitems <= 0)
		return v;

	/* Allocate temporary array for new values */
	values = (char **) palloc(nitems * sizeof(char *));
	MemSet(values, 0, nitems * sizeof(char *));

	/* Loop over source data */
	s = (char *) ARR_DATA_PTR(v);
	for (i = 0; i < nitems; i++)
	{
		/* Get source element */
		if (inp_typbyval)
		{
			switch (inp_typlen)
			{
				case 1:
					elt = (char *) ((long) (*(char *) s));
					break;
				case 2:
					elt = (char *) ((long) (*(int16 *) s));
					break;
				case 3:
				case 4:
				default:
					elt = (char *) ((long)(*(int32 *) s));
					break;
			}
			s += inp_typlen;
		}
		else
		{
			elt = s;
			if (inp_typlen > 0)
				s += inp_typlen;
			else
				s += INTALIGN(*(int32 *) s);
		}

		/*
		 * Apply the given function to source elt and extra args. nargs is
		 * the number of extra args taken by fn().
		 */
		switch (nargs)
		{
			case 0:
				p = (char *) (*fn) (elt);
				break;
			case 1:
				p = (char *) (*fn) (elt, args[0]);
				break;
			case 2:
				p = (char *) (*fn) (elt, args[0], args[1]);
				break;
			case 3:
				p = (char *) (*fn) (elt, args[0], args[1], args[2]);
				break;
			case 4:
			default:
				p = (char *) (*fn) (elt, args[0], args[1], args[2], args[3]);
				break;
		}

		/* Update values and total result size */
		if (typbyval)
		{
			values[i] = (char *) p;
			nbytes += typlen;
		}
		else
		{
			int			len;

			len = ((typlen > 0) ? typlen : INTALIGN(*(int32 *) p));
			/* Needed because _CopyArrayEls tries to pfree items */
			if (p == elt)
			{
				p = (char *) palloc(len);
				memcpy(p, elt, len);
			}
			values[i] = (char *) p;
			nbytes += len;
		}
	}

	/* Allocate and initialize the result array */
	nbytes += ARR_OVERHEAD(ndim);
	result = (ArrayType *) palloc(nbytes);
	MemSet(result, 0, nbytes);

	memcpy((char *) result, (char *) &nbytes, sizeof(int));
	memcpy((char *) ARR_NDIM_PTR(result), (char *) &ndim, sizeof(int));
	memcpy((char *) ARR_DIMS(result), ARR_DIMS(v), 2 * ndim * sizeof(int));

	/* Copy new values into the result array. values is pfreed. */
	_CopyArrayEls((char **) values,
				  ARR_DATA_PTR(result), nitems,
				  typlen, typalign, typbyval);

	return result;
}

/*-----------------------------------------------------------------------------
 * array_eq :
 *		  compares two arrays for equality
 * result :
 *		  returns 1 if the arrays are equal, 0 otherwise.
 *-----------------------------------------------------------------------------
 */
int
array_eq(ArrayType *array1, ArrayType *array2)
{
	if ((array1 == NULL) || (array2 == NULL))
		return 0;
	if (*(int *) array1 != *(int *) array2)
		return 0;
	if (memcmp(array1, array2, *(int *) array1))
		return 0;
	return 1;
}

/***************************************************************************/
/******************|		  Support  Routines			  |*****************/
/***************************************************************************/
static void
system_cache_lookup(Oid element_type,
					bool input,
					int *typlen,
					bool *typbyval,
					char *typdelim,
					Oid *typelem,
					Oid *proc,
					char *typalign)
{
	HeapTuple	typeTuple;
	Form_pg_type typeStruct;

	typeTuple = SearchSysCacheTuple(TYPEOID,
									ObjectIdGetDatum(element_type),
									0, 0, 0);

	if (!HeapTupleIsValid(typeTuple))
	{
		elog(ERROR, "array_out: Cache lookup failed for type %u\n",
			 element_type);
		return;
	}
	typeStruct = (Form_pg_type) GETSTRUCT(typeTuple);
	*typlen = typeStruct->typlen;
	*typbyval = typeStruct->typbyval;
	*typdelim = typeStruct->typdelim;
	*typelem = typeStruct->typelem;
	*typalign = typeStruct->typalign;
	if (input)
		*proc = typeStruct->typinput;
	else
		*proc = typeStruct->typoutput;
}

static Datum
_ArrayCast(char *value, bool byval, int len)
{
	if (byval)
	{
		switch (len)
		{
				case 1:
				return (Datum) *value;
			case 2:
				return (Datum) *(int16 *) value;
			case 3:
			case 4:
				return (Datum) *(int32 *) value;
			default:
				elog(ERROR, "array_ref: byval and elt len > 4!");
				break;
		}
	}
	else
		return (Datum) value;
	return 0;
}


static int
ArrayCastAndSet(char *src,
				bool typbyval,
				int typlen,
				char *dest)
{
	int			inc;

	if (typlen > 0)
	{
		if (typbyval)
		{
			switch (typlen)
			{
				case 1:
					*dest = DatumGetChar(src);
					break;
				case 2:
					*(int16 *) dest = DatumGetInt16(src);
					break;
				case 4:
					*(int32 *) dest = (int32)(long) src;
					break;
			}
		}
		else
			memmove(dest, src, typlen);
		inc = typlen;
	}
	else
	{
		memmove(dest, src, *(int32 *) src);
		inc = (INTALIGN(*(int32 *) src));
	}
	return inc;
}

#ifdef LOARRAY
static char *
_AdvanceBy1word(char *str, char **word)
{
	char	   *retstr,
			   *space;

	*word = NULL;
	if (str == NULL)
		return str;
	while (isspace(*str))
		str++;
	*word = str;
	if ((space = (char *) strchr(str, ' ')) != (char *) NULL)
	{
		retstr = space + 1;
		*space = '\0';
	}
	else
		retstr = NULL;
	return retstr;
}

#endif

static int
SanityCheckInput(int ndim, int n, int *dim, int *lb, int *indx)
{
	int			i;

	/* Do Sanity check on input */
	if (n != ndim)
		return 0;
	for (i = 0; i < ndim; i++)
		if ((lb[i] > indx[i]) || (indx[i] >= (dim[i] + lb[i])))
			return 0;
	return 1;
}

static void
_ArrayRange(int *st,
			int *endp,
			int bsize,
			char *destPtr,
			ArrayType *array,
			int from)
{
	int			n,
			   *dim,
			   *lb,
				st_pos,
				prod[MAXDIM];
	int			span[MAXDIM],
				dist[MAXDIM],
				indx[MAXDIM];
	int			i,
				j,
				inc;
	char	   *srcPtr;

	n = ARR_NDIM(array);
	dim = ARR_DIMS(array);
	lb = ARR_LBOUND(array);
	srcPtr = ARR_DATA_PTR(array);
	for (i = 0; i < n; st[i] -= lb[i], endp[i] -= lb[i], i++);
	mda_get_prod(n, dim, prod);
	st_pos = tuple2linear(n, st, prod);
	srcPtr = array_seek(srcPtr, bsize, st_pos);
	mda_get_range(n, span, st, endp);
	mda_get_offset_values(n, dist, prod, span);
	for (i = 0; i < n; indx[i++] = 0);
	i = j = n - 1;
	inc = bsize;
	do
	{
		srcPtr = array_seek(srcPtr, bsize, dist[j]);
		if (from)
			inc = array_read(destPtr, bsize, 1, srcPtr);
		else
			inc = array_read(srcPtr, bsize, 1, destPtr);
		destPtr += inc;
		srcPtr += inc;
	} while ((j = next_tuple(i + 1, indx, span)) != -1);
}

static int
_ArrayClipCount(int *stI, int *endpI, ArrayType *array)
{
	int			n,
			   *dim,
			   *lb,
				st_pos,
				prod[MAXDIM];
	int			span[MAXDIM],
				dist[MAXDIM],
				indx[MAXDIM];
	int			i,
				j,
				inc,
				st[MAXDIM],
				endp[MAXDIM];
	int			count = 0;
	char	   *ptr;

	n = ARR_NDIM(array);
	dim = ARR_DIMS(array);
	lb = ARR_LBOUND(array);
	ptr = ARR_DATA_PTR(array);
	for (i = 0; i < n; st[i] = stI[i] - lb[i], endp[i] = endpI[i] - lb[i], i++);
	mda_get_prod(n, dim, prod);
	st_pos = tuple2linear(n, st, prod);
	ptr = array_seek(ptr, -1, st_pos);
	mda_get_range(n, span, st, endp);
	mda_get_offset_values(n, dist, prod, span);
	for (i = 0; i < n; indx[i++] = 0);
	i = j = n - 1;
	do
	{
		ptr = array_seek(ptr, -1, dist[j]);
		inc = INTALIGN(*(int32 *) ptr);
		ptr += inc;
		count += inc;
	} while ((j = next_tuple(i + 1, indx, span)) != -1);
	return count;
}

static char *
array_seek(char *ptr, int eltsize, int nitems)
{
	int			i;

	if (eltsize > 0)
		return ptr + eltsize * nitems;
	for (i = 0; i < nitems; i++)
		ptr += INTALIGN(*(int32 *) ptr);
	return ptr;
}

static int
array_read(char *destptr, int eltsize, int nitems, char *srcptr)
{
	int			i,
				inc,
				tmp;

	if (eltsize > 0)
	{
		memmove(destptr, srcptr, eltsize * nitems);
		return eltsize * nitems;
	}
	for (i = inc = 0; i < nitems; i++)
	{
		tmp = (INTALIGN(*(int32 *) srcptr));
		memmove(destptr, srcptr, tmp);
		srcptr += tmp;
		destptr += tmp;
		inc += tmp;
	}
	return inc;
}

static void
_LOArrayRange(int *st,
			  int *endp,
			  int bsize,
			  int srcfd,
			  int destfd,
			  ArrayType *array,
			  int isSrcLO,
			  bool *isNull)
{
	int			n,
			   *dim,
				st_pos,
				prod[MAXDIM];
	int			span[MAXDIM],
				dist[MAXDIM],
				indx[MAXDIM];
	int			i,
				j,
				inc,
				tmp,
			   *lb,
				offset;

	n = ARR_NDIM(array);
	dim = ARR_DIMS(array);
	lb = ARR_LBOUND(array);
	for (i = 0; i < n; st[i] -= lb[i], endp[i] -= lb[i], i++);

	mda_get_prod(n, dim, prod);
	st_pos = tuple2linear(n, st, prod);
	offset = st_pos * bsize;
	if (lo_lseek(srcfd, offset, SEEK_SET) < 0)
		return;
	mda_get_range(n, span, st, endp);
	mda_get_offset_values(n, dist, prod, span);
	for (i = 0; i < n; indx[i++] = 0);
	for (i = n - 1, inc = bsize; i >= 0; inc *= span[i--])
		if (dist[i])
			break;
	j = n - 1;
	do
	{
		offset += (dist[j] * bsize);
		if (lo_lseek(srcfd, offset, SEEK_SET) < 0)
			return;
		tmp = _LOtransfer((char **) &srcfd, inc, 1, (char **) &destfd, isSrcLO, 1);
		if (tmp < inc)
			return;
		offset += inc;
	} while ((j = next_tuple(i + 1, indx, span)) != -1);
}


static void
_ReadArray(int *st,
		   int *endp,
		   int bsize,
		   int srcfd,
		   int destfd,
		   ArrayType *array,
		   int isDestLO,
		   bool *isNull)
{
	int			n,
			   *dim,
				st_pos,
				prod[MAXDIM];
	int			span[MAXDIM],
				dist[MAXDIM],
				indx[MAXDIM];
	int			i,
				j,
				inc,
				tmp,
			   *lb,
				offset;

	n = ARR_NDIM(array);
	dim = ARR_DIMS(array);
	lb = ARR_LBOUND(array);
	for (i = 0; i < n; st[i] -= lb[i], endp[i] -= lb[i], i++);

	mda_get_prod(n, dim, prod);
	st_pos = tuple2linear(n, st, prod);
	offset = st_pos * bsize;
	if (lo_lseek(srcfd, offset, SEEK_SET) < 0)
		return;
	mda_get_range(n, span, st, endp);
	mda_get_offset_values(n, dist, prod, span);
	for (i = 0; i < n; indx[i++] = 0);
	for (i = n - 1, inc = bsize; i >= 0; inc *= span[i--])
		if (dist[i])
			break;
	j = n - 1;
	do
	{
		offset += (dist[j] * bsize);
		if (lo_lseek(srcfd, offset, SEEK_SET) < 0)
			return;
		tmp = _LOtransfer((char **) &destfd, inc, 1, (char **) &srcfd, 1, isDestLO);
		if (tmp < inc)
			return;
		offset += inc;
	} while ((j = next_tuple(i + 1, indx, span)) != -1);
}

int
_LOtransfer(char **destfd,
			int size,
			int nitems,
			char **srcfd,
			int isSrcLO,
			int isDestLO)
{
#define MAX_READ (512 * 1024)
#if !defined(min)
#define min(a, b) (a < b ? a : b)
#endif
	struct varlena *v = NULL;
	int			tmp,
				inc,
				resid;

	inc = nitems * size;
	if (isSrcLO && isDestLO && inc > 0)
		for (tmp = 0, resid = inc;
			 resid > 0 && (inc = min(resid, MAX_READ)) > 0; resid -= inc)
		{
#ifdef LOARRAY
			v = (struct varlena *) LOread((int) *srcfd, inc);
			if (VARSIZE(v) - VARHDRSZ < inc)
			{
				pfree(v);
				return -1;
			}
			tmp += LOwrite((int) *destfd, v);
#endif
			pfree(v);

		}
	else if (!isSrcLO && isDestLO)
	{
		tmp = lo_write((long) *destfd, *srcfd, inc);
		*srcfd = *srcfd + tmp;
	}
	else if (isSrcLO && !isDestLO)
	{
		tmp = lo_read((long) *srcfd, *destfd, inc);
		*destfd = *destfd + tmp;
	}
	else
	{
		memmove(*destfd, *srcfd, inc);
		tmp = inc;
		*srcfd += inc;
		*destfd += inc;
	}
	return tmp;
#undef MAX_READ
}

char *
_array_newLO(int *fd, int flag)
{
	char	   *p;
	char		saveName[NAME_LEN];

	p = (char *) palloc(NAME_LEN);
	sprintf(p, "/Arry.%u", newoid());
	strcpy(saveName, p);
#ifdef LOARRAY
	if ((*fd = LOcreat(saveName, 0600, flag)) < 0)
		elog(ERROR, "Large object create failed");
#endif
	return p;
}
