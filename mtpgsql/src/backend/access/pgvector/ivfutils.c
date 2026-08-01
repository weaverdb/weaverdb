#include "postgres.h"

#include "access/funcindex.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "catalog/index.h"
#include "fmgr.h"
#include "halfutils.h"
#include "halfvec.h"
#include "ivfflat.h"
#include "env/freespace.h"
#include "storage/bufmgr.h"
#include "utils/relcache.h"
#include "utils/varbit.h"
#include "catalog/pg_type.h"
#include "vector.h"

#include "varatt.h"

/*
 * Allocate a vector array
 */
VectorArray
VectorArrayInit(int maxlen, int dimensions, Size itemsize)
{
	VectorArray res = palloc(sizeof(VectorArrayData));

	/* Ensure items are aligned to prevent UB */
	itemsize = MAXALIGN(itemsize);

	res->length = 0;
	res->maxlen = maxlen;
	res->dim = dimensions;
	res->itemsize = itemsize;
	res->items = palloc_extended(maxlen * itemsize, MCXT_ALLOC_ZERO | MCXT_ALLOC_HUGE);
	return res;
}

/*
 * Free a vector array
 */
void
VectorArrayFree(VectorArray arr)
{
	pfree(arr->items);
	pfree(arr);
}

/*
 * Infer vector dimensions from heap data when atttypmod is unset (-1).
 * When finfo is set (functional index), form the indexed value first so
 * blob/bytea columns converted via bytea_to_vector() work.
 */
int
IvfflatInferIndexDimensions(Relation heap, AttrNumber attnum)
{
	return IvfflatInferIndexDimensionsEx(heap, attnum, NULL, NULL);
}

int
IvfflatInferIndexDimensionsEx(Relation heap, AttrNumber attnum,
							  AttrNumber *attnums, FuncIndexInfo *finfo)
{
	HeapScanDesc scan;
	HeapTuple	tup;
	bool		isnull = false;
	Datum		val;
	Form_pg_attribute att;
	int			natts = 1;

	if (heap == NULL || !RelationIsValid(heap))
		return -1;

	if (finfo != NULL && FIisFunctionalIndex(finfo))
		natts = FIgetnArgs(finfo);

	att = TupleDescAttr(RelationGetDescr(heap),
						(finfo != NULL && attnums != NULL) ? attnums[0] - 1 : attnum - 1);

	scan = heap_beginscan(heap, SnapshotNow, 0, (ScanKey) NULL);
	while ((tup = heap_getnext(scan)) != NULL)
	{
		if (finfo != NULL && FIisFunctionalIndex(finfo) && attnums != NULL)
		{
			Datum		values[INDEX_MAX_KEYS];
			char		nulls[INDEX_MAX_KEYS];
			Pointer		ptr;
			Vector	   *vec;
			int			dim;

			FormIndexDatum(natts, attnums, tup, RelationGetDescr(heap),
						   values, nulls, finfo);
			if (nulls[0] == 'n')
				continue;

			ptr = (Pointer) DatumGetPointer(PG_DETOAST_DATUM(values[0]));
			vec = (Vector *) ptr;
			heap_endscan(scan);
			if (vec != NULL && vec->dim > 0 && vec->dim <= VECTOR_MAX_DIM)
				dim = (int) vec->dim;
			else
				return -1;
			if ((Pointer) vec != DatumGetPointer(values[0]))
				pfree(vec);
			return dim;
		}

		val = heap_getattr(tup, attnum, RelationGetDescr(heap), &isnull);
		if (!isnull)
		{
			Pointer		ptr;
			int			dim;

			ptr = (Pointer) DatumGetPointer(PG_DETOAST_DATUM(val));

			if (att->atttypid == VARBITOID || att->atttypid == ZPBITOID)
			{
				VarBit	   *vb = (VarBit *) ptr;

				heap_endscan(scan);
				dim = (int) VARBITLEN(vb);
				if (dim <= 0)
					return -1;
				if ((Pointer) vb != DatumGetPointer(val))
					pfree(vb);
				return dim;
			}

			{
				Vector	   *vec = (Vector *) ptr;

				heap_endscan(scan);
				if (vec != NULL && vec->dim > 0 && vec->dim <= VECTOR_MAX_DIM)
					dim = (int) vec->dim;
				else
					return -1;
				if ((Pointer) vec != DatumGetPointer(val))
					pfree(vec);
				return dim;
			}
		}
	}
	heap_endscan(scan);
	return -1;
}

static Oid		ivfflat_build_indexid = InvalidOid;
static int		ivfflat_build_lists = 0;

void
IvfflatSetBuildLists(Oid indexId, int lists)
{
	ivfflat_build_indexid = indexId;
	ivfflat_build_lists = lists;
}

void
IvfflatClearBuildLists(Oid indexId)
{
	if (ivfflat_build_indexid == indexId)
	{
		ivfflat_build_indexid = InvalidOid;
		ivfflat_build_lists = 0;
	}
}

/*
 * Get the number of lists in the index
 */
int
IvfflatGetLists(Relation index)
{
	if (ivfflat_build_indexid == RelationGetRelid(index) && ivfflat_build_lists > 0)
		return ivfflat_build_lists;

	if (RelationGetNumberOfBlocks(index) > 0)
	{
		int			lists = 0;
		int			dimensions = 0;

		IvfflatGetMetaPageInfo(index, &lists, &dimensions);
		if (lists > 0)
			return lists;
	}

	return IVFFLAT_DEFAULT_LISTS;
}

/*
 * Get proc
 */
FmgrInfo *
IvfflatOptionalProcInfo(Relation index, uint16 procnum)
{
	if (!OidIsValid(index_getprocid(index, 1, procnum)))
		return NULL;

	return index_getprocinfo(index, 1, procnum);
}

/*
 * Normalize value
 */
Datum
IvfflatNormValue(const IvfflatTypeInfo * typeInfo, Oid collation, Datum value)
{
	return DirectFunctionCall1Coll(typeInfo->normalize, collation, value);
}

/*
 * Check if non-zero norm
 */
bool
IvfflatCheckNorm(FmgrInfo *procinfo, Oid collation, Datum value)
{
	return DatumGetFloat8(FunctionCall1Coll(procinfo, collation, value)) > 0;
}

/*
 * New buffer
 */
Buffer
IvfflatNewBuffer(Relation index, ForkNumber forkNum)
{
	Buffer		buf = ReadBufferExtended(index, forkNum, P_NEW, RBM_NORMAL, NULL);

	LockBuffer(index, buf, BUFFER_LOCK_EXCLUSIVE);
	return buf;
}

/*
 * Init page
 */
void
IvfflatInitPage(Buffer buf, Page page)
{
	PageInit(page, BufferGetPageSize(buf), sizeof(IvfflatPageOpaqueData));
	IvfflatPageGetOpaque(page)->nextblkno = InvalidBlockNumber;
	IvfflatPageGetOpaque(page)->page_id = IVFFLAT_PAGE_ID;
}

/*
 * Init and register page
 */
void
IvfflatInitRegisterPage(Relation index, Buffer *buf, Page *page)
{
	*page = BufferGetPage(*buf);
	IvfflatInitPage(*buf, *page);
}

/*
 * Commit buffer
 */
void
IvfflatCommitBuffer(Relation index, Buffer buff)
{
	LockBuffer(index, buff, BUFFER_LOCK_UNLOCK);
	WriteBuffer(index, buff);
}

/*
 * Add a new page
 *
 * The order is very important!!
 */
void
IvfflatAppendPage(Relation index, Buffer *buf, Page *page, ForkNumber forkNum)
{
	/* Get new buffer */
	Buffer		newbuf = IvfflatNewBuffer(index, forkNum);
	Page		newpage = BufferGetPage(newbuf);

	/* Update the previous buffer */
	IvfflatPageGetOpaque(*page)->nextblkno = BufferGetBlockNumber(newbuf);

	/* Init new page */
	IvfflatInitPage(newbuf, newpage);

	/* Commit previous page */
	LockBuffer(index, *buf, BUFFER_LOCK_UNLOCK);
	WriteBuffer(index, *buf);

	*page = BufferGetPage(newbuf);
	*buf = newbuf;
}

/*
 * Get the metapage info
 */
void
IvfflatGetMetaPageInfo(Relation index, int *lists, int *dimensions)
{
	Buffer		buf;
	Page		page;
	IvfflatMetaPage metap;

	buf = ReadBuffer(index, IVFFLAT_METAPAGE_BLKNO);
	LockBuffer(index, buf, BUFFER_LOCK_SHARE);
	page = BufferGetPage(buf);
	metap = IvfflatPageGetMeta(page);

	if (unlikely(metap->magicNumber != IVFFLAT_MAGIC_NUMBER))
		elog(ERROR, "ivfflat index is not valid");

	if (lists != NULL)
		*lists = metap->lists;

	if (dimensions != NULL)
		*dimensions = metap->dimensions;

	LockBuffer(index, buf, BUFFER_LOCK_UNLOCK);
	ReleaseBuffer(index, buf);
}

/*
 * Update the start or insert page of a list
 */
void
IvfflatUpdateList(Relation index, ListInfo listInfo,
				  BlockNumber insertPage, BlockNumber originalInsertPage,
				  BlockNumber startPage, ForkNumber forkNum)
{
	Buffer		buf;
	Page		page;
	IvfflatList list;
	bool		changed = false;

	buf = ReadBufferExtended(index, forkNum, listInfo.blkno, RBM_NORMAL, NULL);
	LockBuffer(index, buf, BUFFER_LOCK_EXCLUSIVE);
	page = BufferGetPage(buf);
	list = (IvfflatList) PageGetItem(page, PageGetItemId(page, listInfo.offno));

	if (BlockNumberIsValid(insertPage) && insertPage != list->insertPage)
	{
		/* Skip update if insert page is lower than original insert page  */
		/* This is needed to prevent insert from overwriting vacuum */
		if (!BlockNumberIsValid(originalInsertPage) || insertPage >= originalInsertPage)
		{
			list->insertPage = insertPage;
			changed = true;
		}
	}

	if (BlockNumberIsValid(startPage) && startPage != list->startPage)
	{
		list->startPage = startPage;
		changed = true;
	}

	/* Only commit if changed */
	if (changed)
		IvfflatCommitBuffer(index, buf);
	else
	{
		LockBuffer(index, buf, BUFFER_LOCK_UNLOCK);
		ReleaseBuffer(index, buf);
	}
}

PGDLLEXPORT Datum l2_normalize(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum halfvec_l2_normalize(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum sparsevec_l2_normalize(PG_FUNCTION_ARGS);

static Size
VectorItemSize(int dimensions)
{
	return VECTOR_SIZE(dimensions);
}

static Size
HalfvecItemSize(int dimensions)
{
	return HALFVEC_SIZE(dimensions);
}

static Size
BitItemSize(int dimensions)
{
	return VARBITTOTALLEN(dimensions);
}

static void
VectorUpdateCenter(Pointer v, int dimensions, float *x)
{
	Vector	   *vec = (Vector *) v;

	SET_VARSIZE(vec, VECTOR_SIZE(dimensions));
	vec->dim = dimensions;

	for (int i = 0; i < dimensions; i++)
		vec->x[i] = x[i];
}

static void
HalfvecUpdateCenter(Pointer v, int dimensions, float *x)
{
	HalfVector *vec = (HalfVector *) v;

	SET_VARSIZE(vec, HALFVEC_SIZE(dimensions));
	vec->dim = dimensions;

	for (int i = 0; i < dimensions; i++)
		vec->x[i] = Float4ToHalfUnchecked(x[i]);
}

static void
BitUpdateCenter(Pointer v, int dimensions, float *x)
{
	VarBit	   *vec = (VarBit *) v;
	unsigned char *nx = VARBITS(vec);

	SET_VARSIZE(vec, VARBITTOTALLEN(dimensions));
	VARBITLEN(vec) = dimensions;

	for (uint32 i = 0; i < VARBITBYTES(vec); i++)
		nx[i] = 0;

	for (int i = 0; i < dimensions; i++)
		nx[i / 8] |= (x[i] > 0.5 ? 1 : 0) << (7 - (i % 8));
}

static void
VectorSumCenter(Pointer v, float *x)
{
	Vector	   *vec = (Vector *) v;
	int			dim = vec->dim;

	/* Auto-vectorized */
	for (int i = 0; i < dim; i++)
		x[i] += vec->x[i];
}

static void
HalfvecSumCenter(Pointer v, float *x)
{
	HalfVector *vec = (HalfVector *) v;
	int			dim = vec->dim;

	/* Auto-vectorized on aarch64 */
	for (int i = 0; i < dim; i++)
		x[i] += HalfToFloat4(vec->x[i]);
}

static void
BitSumCenter(Pointer v, float *x)
{
	VarBit	   *vec = (VarBit *) v;

	for (int i = 0; i < VARBITLEN(vec); i++)
		x[i] += (float) (((VARBITS(vec)[i / 8]) >> (7 - (i % 8))) & 0x01);
}

/*
 * Get type info
 */
const		IvfflatTypeInfo *
IvfflatGetTypeInfo(Relation index)
{
	FmgrInfo   *procinfo = IvfflatOptionalProcInfo(index, IVFFLAT_TYPE_INFO_PROC);

	if (procinfo == NULL)
	{
		static const IvfflatTypeInfo typeInfo = {
			.maxDimensions = IVFFLAT_MAX_DIM,
			.normalize = l2_normalize,
			.itemSize = VectorItemSize,
			.updateCenter = VectorUpdateCenter,
			.sumCenter = VectorSumCenter
		};

		return (&typeInfo);
	}
	else
		return (const IvfflatTypeInfo *) DatumGetPointer(FunctionCall0Coll(procinfo, InvalidOid));
}

FUNCTION_PREFIX PG_FUNCTION_INFO_V1(ivfflat_halfvec_support);
Datum
ivfflat_halfvec_support(PG_FUNCTION_ARGS)
{
	static const IvfflatTypeInfo typeInfo = {
		.maxDimensions = IVFFLAT_MAX_DIM * 2,
		.normalize = halfvec_l2_normalize,
		.itemSize = HalfvecItemSize,
		.updateCenter = HalfvecUpdateCenter,
		.sumCenter = HalfvecSumCenter
	};

	PG_RETURN_POINTER(&typeInfo);
}

FUNCTION_PREFIX PG_FUNCTION_INFO_V1(ivfflat_bit_support);
Datum
ivfflat_bit_support(PG_FUNCTION_ARGS)
{
	static const IvfflatTypeInfo typeInfo = {
		.maxDimensions = IVFFLAT_MAX_DIM * 32,
		.normalize = NULL,
		.itemSize = BitItemSize,
		.updateCenter = BitUpdateCenter,
		.sumCenter = BitSumCenter
	};

	PG_RETURN_POINTER(&typeInfo);
}
