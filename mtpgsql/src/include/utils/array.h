/*-------------------------------------------------------------------------
 *
 * array.h
 *	  Utilities for the new array code. Contain prototypes from the
 *	  following files:
 *				utils/adt/arrayfuncs.c
 *				utils/adt/arrayutils.c
 *				utils/adt/chunk.c
 *
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 * NOTES
 *	  XXX the data array should be LONGALIGN'd -- notice that the array
 *	  allocation code does not allocate the extra space required for this,
 *	  even though the array-packing code does the LONGALIGNs.
 *
 *-------------------------------------------------------------------------
 */
#ifndef ARRAY_H
#define ARRAY_H


#include "utils/memutils.h"

typedef struct
{
	int			size;			/* total array size (in bytes) */
	int			ndim;			/* # of dimensions */
	int			flags;			/* implementation flags */
} ArrayType;

/*
 * bitmask of ArrayType flags field:
 * 1st bit - large object flag
 * 2nd bit - chunk flag (array is chunked if set)
 * 3rd,4th,&5th bit - large object type (used only if bit 1 is set)
 */
#define ARR_LOB_FLAG	(0x1)
#define ARR_CHK_FLAG	(0x2)
#define ARR_OBJ_MASK	(0x1c)

#define ARR_FLAGS(a)			((ArrayType *) a)->flags
#define ARR_SIZE(a)				(((ArrayType *) a)->size)

#define ARR_NDIM(a)				(((ArrayType *) a)->ndim)
#define ARR_NDIM_PTR(a)			(&(((ArrayType *) a)->ndim))

#define ARR_IS_LO(a) \
		(((ArrayType *) a)->flags & ARR_LOB_FLAG)
#define SET_LO_FLAG(f,a) \
		(((ArrayType *) a)->flags |= ((f) ? ARR_LOB_FLAG : 0x0))

#define ARR_IS_CHUNKED(a) \
		(((ArrayType *) a)->flags & ARR_CHK_FLAG)
#define SET_CHUNK_FLAG(f,a) \
		(((ArrayType *) a)->flags |= ((f) ? ARR_CHK_FLAG : 0x0))

#define ARR_OBJ_TYPE(a) \
		((ARR_FLAGS(a) & ARR_OBJ_MASK) >> 2)
#define SET_OBJ_TYPE(f,a) \
		((ARR_FLAGS(a)&= ~ARR_OBJ_MASK), (ARR_FLAGS(a)|=((f<<2)&ARR_OBJ_MASK)))

/*
 * ARR_DIMS returns a pointer to an array of array dimensions (number of
 * elements along the various array axes).
 *
 * ARR_LBOUND returns a pointer to an array of array lower bounds.
 *
 * That is: if the third axis of an array has elements 5 through 10, then
 * ARR_DIMS(a)[2] == 6 and ARR_LBOUND[2] == 5.
 *
 * Unlike C, the default lower bound is 1.
 */
#define ARR_DIMS(a) \
		((int *) (((char *) a) + sizeof(ArrayType)))
#define ARR_LBOUND(a) \
		((int *) (((char *) a) + sizeof(ArrayType) + \
				  (sizeof(int) * (((ArrayType *) a)->ndim))))

/*
 * Returns a pointer to the actual array data.
 */
#define ARR_DATA_PTR(a) \
		(((char *) a) + \
		 MAXALIGN(sizeof(ArrayType) + 2 * (sizeof(int) * (a)->ndim)))

/*
 * The total array header size for an array of dimension n (in bytes).
 */
#define ARR_OVERHEAD(n) \
		(MAXALIGN(sizeof(ArrayType) + 2 * (n) * sizeof(int)))

/*------------------------------------------------------------------------
 * Miscellaneous helper definitions and routines for arrayfuncs.c
 *------------------------------------------------------------------------
 */

#define RETURN_NULL do {*isNull = true; return(0); } while (0)
#define NAME_LEN	30
#define MAX_BUFF_SIZE BLCKSZ

typedef struct
{
	char		lo_name[NAME_LEN];
	int			C[MAXDIM];
} CHUNK_INFO;

/*
 * prototypes for functions defined in arrayfuncs.c
 */
PG_EXTERN char *array_in(char *string, Oid element_type, int32 typmod);
PG_EXTERN char *array_out(ArrayType *v, Oid element_type);
PG_EXTERN char *array_dims(ArrayType *v, bool *isNull);
PG_EXTERN Datum array_ref(ArrayType *array, int n, int *indx, int reftype,
		  int elmlen, int arraylen, bool *isNull);
PG_EXTERN Datum array_clip(ArrayType *array, int n, int *upperIndx,
		   int *lowerIndx, int reftype, int len, bool *isNull);
PG_EXTERN char *array_set(ArrayType *array, int n, int *indx, char *dataPtr,
		  int reftype, int elmlen, int arraylen, bool *isNull);
PG_EXTERN char *array_assgn(ArrayType *array, int n, int *upperIndx,
			int *lowerIndx, ArrayType *newArr, int reftype,
			int len, bool *isNull);
PG_EXTERN ArrayType *array_map(ArrayType *v, Oid type,
		  char *(*fn) (),
		  Oid retType, int nargs,...);
PG_EXTERN int	array_eq(ArrayType *array1, ArrayType *array2);
PG_EXTERN int _LOtransfer(char **destfd, int size, int nitems, char **srcfd,
			int isSrcLO, int isDestLO);

PG_EXTERN char *_array_newLO(int *fd, int flag);


/*
 * prototypes for functions defined in arrayutils.c
 * [these names seem to be too generic. Add prefix for arrays? -- AY]
 */

PG_EXTERN int	GetOffset(int n, int *dim, int *lb, int *indx);
PG_EXTERN int	getNitems(int n, int *a);
PG_EXTERN int	compute_size(int *st, int *endp, int n, int base);
PG_EXTERN void mda_get_offset_values(int n, int *dist, int *PC, int *span);
PG_EXTERN void mda_get_range(int n, int *span, int *st, int *endp);
PG_EXTERN void mda_get_prod(int n, int *range, int *P);
PG_EXTERN int	tuple2linear(int n, int *tup, int *scale);
PG_EXTERN void array2chunk_coord(int n, int *C, int *a_coord, int *c_coord);
PG_EXTERN int	next_tuple(int n, int *curr, int *span);

/*
 * prototypes for functions defined in chunk.c
 */
PG_EXTERN char *_ChunkArray(int fd, FILE *afd, int ndim, int *dim, int baseSize,
			int *nbytes, char *chunkfile);
PG_EXTERN int _ReadChunkArray(int *st, int *endp, int bsize, int fp,
			 char *destfp, ArrayType *array, int isDestLO, bool *isNull);
PG_EXTERN struct varlena *_ReadChunkArray1El(int *st, int bsize, int fp,
				   ArrayType *array, bool *isNull);


#endif	 /* ARRAY_H */
