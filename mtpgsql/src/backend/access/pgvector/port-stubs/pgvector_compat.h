/* Compatibility shims for integrating modern pgvector into WeaverDB's old PG7 fork */

#include <string.h>
#include <errno.h>

/* Early ereport neuter for text inside this header before the real bridge below.
 * After postgres.h-level symbols are available, we redefine to elog/coded_elog.
 */
#define ereport(elevel, rest)   ((void)0)
#define errcode(x)              (0)
#define errmsg(...)             (0)
#define errdetail(...)          (0)
#define errhint(...)            (0)

#ifndef VALGRIND_MAKE_MEM_DEFINED
#define VALGRIND_MAKE_MEM_DEFINED(p, n) ((void) 0)
#endif

/* Dummies for varbit / float return macros used inside bitvec.c / halfvec etc (from CMake dummies + more) */
#define PG_RETURN_FLOAT8(x)     return (Datum)0

#define PG_FREE_IF_COPY(x,n)    ((void)0)
#define PG_GETARG_POINTER(n)    ((void*)0)  /* for some GETARG cases */
#define PG_RETURN_CSTRING(x)    return (Datum)0
#define pg_ltoa(i,ptr)          ((void)0)

#define PG_RETURN_BYTEA_P(x)    return (Datum)0 /* replaced below after PG7 arg wiring */
#define PG_RETURN_POINTER(x)    return (Datum)0

#define PG_RETURN_INT32(x)      return (Datum)0
/*
 * pq_begintypsend / pq_endtypsend / pq_sendfloat4 / pq_getmsgint /
 * pq_getmsgfloat4 are real helpers in libpq/pqformat.c (typreceive/typsend).
 * Do not stub them: no-ops returned NULL bytea / zero vectors.
 */

#define float_overflow_error()  ((void)0)
#define float_underflow_error() ((void)0)

#define PG_RETURN_BOOL(x)       return (Datum)0

#define PG_RETURN_ARRAYTYPE_P(x) return (Datum)0
#define PG_RETURN_NULL()        return (Datum)0

#ifndef ERANGE
#define ERANGE 34
#endif

/* Fmgr direct calls used in debug / out paths */
#define DirectFunctionCall1(f,arg) ((Datum)0)
#define DirectFunctionCall1Coll(f,coll,arg) ((Datum)0)

/* Typmod helpers used in sparsevec typmod parsing */
#define ArrayGetIntegerTypmods(a, n) (NULL)

/* Pull varatt shims (SET_VARSIZE, VARSIZE_ANY, PG_DETOAST etc) early for all pgvector .c
   (their own #include "varatt.h" is inside #if PG_VERSION_NUM >= 160000 which we set to 13). */
#include "varatt.h"

/* Robust neutering of PG extension macros that the pgvector sources use for their
   PG_FUNCTION_INFO / PG_MODULE_MAGIC / PG_FUNCTION_ARGS lines.  Doing it here (early)
   is more reliable than command-line -D across the many headers and C11 strictness.
 */
#undef PG_MODULE_MAGIC
#define PG_MODULE_MAGIC
#undef PG_FUNCTION_INFO_V1
#define PG_FUNCTION_INFO_V1(name)
#undef PG_FUNCTION_ARGS
#define PG_FUNCTION_ARGS void *fcinfo __attribute__((unused))

/* Provide common fmgr arg getters used in the in/out functions so they don't appear undeclared */
#define PG_GETARG_CSTRING(n)   ((char *) 0)
#define PG_GETARG_INT32(n)     (0)
#define PG_GETARG_INT16(n)     (0)
#define PG_GETARG_BOOL(n)      (0)
#define PG_GETARG_DATUM(n)     ((Datum)0)
#define PG_GETARG_POINTER(n)   ((void*)0)


/* FLEXIBLE_ARRAY_MEMBER for old compilers */
#ifndef FLEXIBLE_ARRAY_MEMBER
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define FLEXIBLE_ARRAY_MEMBER 
#else
#define FLEXIBLE_ARRAY_MEMBER 1
#endif
#endif

/* Size/Datum/Page come from c.h / storage/page.h via postgres.h below — do not
 * typedef them here (#ifndef Size does not guard typedefs and causes redefs). */

#ifndef FLOAT8PASSBYVAL
#define FLOAT8PASSBYVAL true
#endif

#ifndef TYPALIGN_INT
#define TYPALIGN_INT 'i'
#endif
#ifndef TYPALIGN_DOUBLE
#define TYPALIGN_DOUBLE 'd'
#endif

/*
 * SQLSTATE-like codes for coded_elog / Env->errorcode.
 * Keep non-zero so ERROR longjmp uses the code (0 falls back to 100 in elog.c).
 */
#ifdef ERRCODE_DATA_EXCEPTION
#undef ERRCODE_DATA_EXCEPTION
#endif
#define ERRCODE_DATA_EXCEPTION				820
#ifndef ERRCODE_PROGRAM_LIMIT_EXCEEDED
#define ERRCODE_PROGRAM_LIMIT_EXCEEDED		821
#endif
#ifndef ERRCODE_INVALID_TEXT_REPRESENTATION
#define ERRCODE_INVALID_TEXT_REPRESENTATION	822
#endif
#ifndef ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE
#define ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE	823
#endif
#ifndef ERRCODE_INVALID_PARAMETER_VALUE
#define ERRCODE_INVALID_PARAMETER_VALUE		824
#endif
#ifndef ERRCODE_NULL_VALUE_NOT_ALLOWED
#define ERRCODE_NULL_VALUE_NOT_ALLOWED		825
#endif
#ifndef ERRCODE_FEATURE_NOT_SUPPORTED
#define ERRCODE_FEATURE_NOT_SUPPORTED		826
#endif

/* Modern PG elevels used by pgvector; map to PG7 elog levels in pgvector_ereport(). */
#ifndef DEBUG1
#define DEBUG1 10
#endif
#ifndef INFO
#define INFO NOTICE
#endif
#ifndef WARNING
#define WARNING NOTICE
#endif

/* Forward declare so VarBit name and ArrayType (from real headers) can be used in prototypes
   without pulling heavy includes at -include time (which happens before postgres.h in the .c). */
struct varbita;
typedef struct varbita VarBit;

/* Index AM vacuum types — full defs after postgres.h.
 * IndexBuildResult / IndexUniqueCheck live in pgvector_index.h. */
typedef struct IndexBulkDeleteResult IndexBulkDeleteResult;
typedef struct IndexVacuumInfo IndexVacuumInfo;

/* Storage / access types used in hnsw.h and ivfflat.h (vacuum, newbuffer, etc) */
typedef unsigned int ForkNumber;

#define MAIN_FORKNUM 0
#define INIT_FORKNUM 1

/* GUC / reloption registration shims for ivfflat.c _PG_init */
typedef struct config_enum_entry
{
	const char *name;
	int			val;
	bool		hidden;
} config_enum_entry;

typedef int GucSource;

extern void DefineCustomIntVariable(const char *name,
	const char *short_desc,
	const char *long_desc,
	int *valueAddr,
	int bootValue,
	int minValue,
	int maxValue,
	int context,
	int flags,
	void (*check_hook) (int *newval, void **extra, GucSource source),
	void (*assign_hook) (int newval, void *extra),
	const char *(*show_hook) (void));

extern void DefineCustomEnumVariable(const char *name,
	const char *short_desc,
	const char *long_desc,
	int *valueAddr,
	int bootValue,
	const struct config_enum_entry *options,
	int context,
	int flags,
	void (*check_hook) (int *newval, void **extra, GucSource source),
	void (*assign_hook) (int newval, void *extra),
	const char *(*show_hook) (void));

extern void EmitWarningsOnPlaceholders(const char *className);
extern void MarkGUCPrefixReserved(const char *className);

/* Progress and optimizer types for cost/ build (replaces commands/progress.h) */
#define PROGRESS_CREATEIDX_SUBPHASE_INITIALIZE 0
#define PROGRESS_CREATEIDX_SUBPHASE 0
#define PROGRESS_CREATEIDX_TUPLES_TOTAL 0
#define PROGRESS_CREATEIDX_TUPLES_DONE 0
/* RelOptInfo: nodes/relation.h. PlannerInfo is modern-only; IndexPath needed
 * early for ivfflat.c/hnsw.c costestimate signatures (also in relation.h). */
typedef struct PlannerInfo PlannerInfo;
typedef struct IndexPath IndexPath;

/* CurrentMemoryContext provided by real headers */
#ifndef CurrentMemoryContext
#define CurrentMemoryContext (MemoryContextGetTopContext())
#endif
#define ALLOCSET_DEFAULT_SIZES ALLOCSET_DEFAULT_MINSIZE, ALLOCSET_DEFAULT_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE

/* AllocSetContextCreate provided by real headers when needed; dummy macro covers calls */
typedef struct BufferAccessStrategyData *BufferAccessStrategy;

/* itemptr callback — full ItemPointer after postgres.h; use void* here */
typedef bool (*IndexBulkDeleteCallback) (void *itemptr, void *callback_state);

/* List / Page / Buffer / BlockNumber come from real headers via postgres.h. */

/* DSM / shared memory types (upstream parallel unused; Weaver uses pthreads) */
typedef struct dsm_segment dsm_segment;
typedef struct shm_toc shm_toc;

/* Sharedsort declared in pgvector_tuplesort.h */

/* Ensure VARSIZE_ANY visible even if varatt.h include is version-guarded in ivfflat.h */
#ifndef VARSIZE_ANY
#define VARSIZE_ANY(ptr) VARSIZE(ptr)
#endif

/* VarBit type alias provided via forward + real header will complete the struct.
   Do *not* #include "utils/varbit.h" here - it would pull in headers too early. */

/* VARBITTOTALLEN from modern pg */
#ifndef VARBITTOTALLEN
#define VARBITTOTALLEN(BITLEN)   (VARBITDATALEN(BITLEN) )
#endif

/* Other common missing */
#ifndef PG_USED_FOR_ASSERTS_ONLY
#define PG_USED_FOR_ASSERTS_ONLY
#endif

/* Atomics / popcount: mtpgsql/src/include/port/{atomics,pg_bitutils}.h */

/* ----------------------------------------------------------------
 * Additional shims for array handling, fmgr, varlena, oids etc.
 * This header is force-included via -include for all pgvector compilation units.
 * ---------------------------------------------------------------- */

/* Make sure basic varlena access from fork is visible */
#include "postgres.h"
#include "fmgr.h"
#include "utils/array.h"
#include "catalog/pg_type.h"
#include "access/itup.h"
#include "access/tupdesc.h"

extern char *fmgr_c(FmgrInfo *finfo, FmgrValues *values, bool *isNull);

/*
 * PG7 fmgr passes SQL function args as (long, long, ...) and expects char *
 * returns.  pgvector sources use PG13-style PG_GETARG_* / PG_RETURN_* on a
 * fake fcinfo; wire those macros to the real register args at runtime.
 */
#undef PG_FUNCTION_ARGS
#define PG_FUNCTION_ARGS char *pgvector_arg0, long pgvector_arg1, long pgvector_arg2

#undef PG_GETARG_CSTRING
#define PG_GETARG_CSTRING(n) \
	((char *) ((n) == 0 ? (long) pgvector_arg0 : \
	 (n) == 1 ? pgvector_arg1 : pgvector_arg2))

#undef PG_GETARG_INT32
#define PG_GETARG_INT32(n) \
	((int32) ((n) == 2 ? pgvector_arg2 : (n) == 1 ? pgvector_arg1 : 0))

#undef PG_GETARG_INT16
#define PG_GETARG_INT16(n) ((int16) PG_GETARG_INT32(n))

#undef PG_GETARG_BOOL
#define PG_GETARG_BOOL(n) ((bool) PG_GETARG_INT32(n))

#undef PG_GETARG_FLOAT8
/* float8 is pass-by-ref (float64*) in this fork — DatumGetFloat8 derefs the pointer */
#define PG_GETARG_FLOAT8(n) DatumGetFloat8(PG_GETARG_DATUM(n))

#undef PG_GETARG_DATUM
#define PG_GETARG_DATUM(n) \
	((Datum) (long) (((n) == 0) ? (long) pgvector_arg0 : \
	 (((n) == 1) ? (long) pgvector_arg1 : (long) pgvector_arg2)))

#undef PG_GETARG_POINTER
#define PG_GETARG_POINTER(n) ((void *) PG_GETARG_DATUM(n))

#undef PG_GETARG_VARBIT_P
#define PG_GETARG_VARBIT_P(n)	((VarBit *) PG_GETARG_POINTER(n))

#undef PG_RETURN_VARBIT_P
#define PG_RETURN_VARBIT_P(x)	PG_RETURN_POINTER(x)

#undef PG_RETURN_POINTER
#define PG_RETURN_POINTER(x) return (Datum) (long) (x)

#undef PG_RETURN_BYTEA_P
#define PG_RETURN_BYTEA_P(x) PG_RETURN_POINTER(x)

#undef PG_GETARG_BYTEA_P
#define PG_GETARG_BYTEA_P(n) \
	((bytea *) DatumGetPointer(PG_DETOAST_DATUM(PG_GETARG_DATUM(n))))

#undef PG_RETURN_CSTRING
#define PG_RETURN_CSTRING(x) return (Datum) (long) (x)

#undef PG_RETURN_NULL
#define PG_RETURN_NULL() return (Datum) 0

#undef PG_RETURN_INT32
/* int4 is pass-by-value in this fork; return the integer in the Datum/register */
#define PG_RETURN_INT32(x) return (Datum) (long) (int32) (x)

#undef PG_RETURN_BOOL
/* bool is pass-by-value (len 1); must not return a palloc'd pointer */
#define PG_RETURN_BOOL(x) return (Datum) (long) ((x) ? 1 : 0)

#undef PG_RETURN_FLOAT8
#define PG_RETURN_FLOAT8(x) \
	do { \
		float64 _pgv_r = (float64) palloc(sizeof(float64data)); \
		*_pgv_r = (double) (x); \
		return (Datum) (long) _pgv_r; \
	} while (0)

/*
 * Bridge modern ereport(errmsg(...)) to Weaver elog/coded_elog.
 * Evaluate `rest` for side effects (errcode/errmsg/errdetail/errhint fill TLS),
 * then emit through the existing error infrastructure.
 */
extern void pgvector_err_reset(void);
extern void pgvector_ereport(int elevel);
extern int	pgvector_errcode(int sqlerrcode);
extern int	pgvector_errmsg(const char *fmt,...)
#if defined(__GNUC__)
			__attribute__((format(printf, 1, 2)))
#endif
			;
extern int	pgvector_errdetail(const char *fmt,...)
#if defined(__GNUC__)
			__attribute__((format(printf, 1, 2)))
#endif
			;
extern int	pgvector_errhint(const char *fmt,...)
#if defined(__GNUC__)
			__attribute__((format(printf, 1, 2)))
#endif
			;

/* Used inside errmsg() args now that ereport evaluates its rest expression. */
extern char *pgvector_pnstrdup(const char *in, size_t len);
#ifndef pnstrdup
#define pnstrdup(in, len) pgvector_pnstrdup((in), (size_t) (len))
#endif

#undef ereport
#undef errcode
#undef errmsg
#undef errdetail
#undef errhint
#define errcode(code)			pgvector_errcode(code)
#define errmsg(...)				pgvector_errmsg(__VA_ARGS__)
#define errdetail(...)			pgvector_errdetail(__VA_ARGS__)
#define errhint(...)			pgvector_errhint(__VA_ARGS__)
#define ereport(elevel, rest) \
	do { \
		pgvector_err_reset(); \
		(void) (rest); \
		pgvector_ereport(elevel); \
	} while (0)

#undef DirectFunctionCall1
#undef DirectFunctionCall1Coll

static inline Datum
pgvector_direct_call1(void *fn, Datum arg1)
{
	typedef char *(*pgvector_fn1) (long);

	return (Datum) (long) ((pgvector_fn1) fn)((long) arg1);
}

#define DirectFunctionCall1(func, arg1) \
	pgvector_direct_call1((void *) (func), (arg1))

static inline Datum
DirectFunctionCall1Coll(void *func, Oid coll, Datum arg1)
{
	(void) coll;
	return pgvector_direct_call1(func, arg1);
}

#define TupleDescAttr(tupdesc, i) ((tupdesc)->attrs[(i)])

/* Complete vacuum/AM result types now that BlockNumber/Relation exist */
struct IndexBulkDeleteResult
{
	BlockNumber num_pages;
	double		num_index_tuples;
	BlockNumber pages_removed;
	double		tuples_removed;
	BlockNumber pages_deleted;
	BlockNumber pages_newly_deleted;
	BlockNumber pages_free;
};

struct IndexVacuumInfo
{
	Relation	index;
	bool		analyze_only;
	bool		report_progress;
	bool		estimated_count;
	int			message_level;
	double		num_heap_tuples;
	BufferAccessStrategy strategy;
};

/* ---- ArrayType / ARR_* shims to let pgvector code parse and do basic 1-d float work ---- */
/* The project's ArrayType is different (chunked/LOB oriented). We provide the
   field names and macros pgvector expects by using a wrapper or by direct
   interpretation where possible. For first-cut build we synthesize a fake
   header layout on top when needed. */

/* In modern PG ArrayType has these; map or provide accessors */
#ifndef ARR_ELEMTYPE
/* We will synthesize via a compat structure when deconstructing. For macros in
   conditions we provide dummies that allow the common 1-d float path. */
#define ARR_ELEMTYPE(a)     (FLOAT4OID)   /* lie for common path; real path uses deconstruct */
#endif

#ifndef ARR_HASNULL
#define ARR_HASNULL(a)      (0)
#endif

#ifndef ARR_OVERHEAD
/* already in project array.h but different signature sometimes */
#endif

/* PG_GETARG_ARRAYTYPE_P */
#ifndef PG_GETARG_ARRAYTYPE_P
#define PG_GETARG_ARRAYTYPE_P(n)  ((ArrayType *) PG_GETARG_POINTER(n))
#endif

/* Some code uses PointerGetDatum etc - assume fmgr.h / postgres.h has them */

/* get_float8_infinity used for index cost estimates */
static inline double
get_float8_infinity(void)
{
	return (double) 1.0 / 0.0;   /* INFINITY without <math.h> issues in all C stds */
}

/* ---- Halfvec support: halfutils.h provides Float4ToHalf / HalfToFloat4 when halfutils.c is linked ---- */

/* For index AM relation options etc that may pull in */
#ifndef RELOPT_KIND_IVFFLAT
#define RELOPT_KIND_IVFFLAT  (1 << 10)   /* arbitrary high bit */
#endif
#ifndef RELOPT_KIND_HNSW
#define RELOPT_KIND_HNSW     (1 << 11)
#endif

/* end of pgvector_compat.h shims (ereport bridged to elog above) */

/* ----------------------------------------------------------------
 * Datum <-> float4/float8 accessors - prototypes only here (force include time).
 * Full definitions (using memcpy on Datum bits) are in pgvector_shims.c so they
 * compile after the translation unit has seen c.h / postgres.h definitions of Datum.
 * ---------------------------------------------------------------- */
extern float4 DatumGetFloat4(Datum X);
extern Datum  Float4GetDatum(float4 X);
extern float8 DatumGetFloat8(Datum X);
extern Datum  Float8GetDatum(float8 X);

/* Common pointer/datum casts - provide if not already after includes */
#ifndef PointerGetDatum
#define PointerGetDatum(X)  ((Datum) (X))
#endif
#ifndef DatumGetPointer
#define DatumGetPointer(X)  ((Pointer) (X))
#endif

/* ----------------------------------------------------------------
 * Weaver buffer manager compatibility (no GenericXLog / ReadBufferExtended)
 * ---------------------------------------------------------------- */
#ifndef BUFFER_UNLOCK
#define BUFFER_UNLOCK BUFFER_LOCK_UNLOCK
#endif
#ifndef BUFFER_NOLOCK
#define BUFFER_NOLOCK BUFFER_LOCK_UNLOCK
#endif
#ifndef RBM_NORMAL
#define RBM_NORMAL 0
#endif

/* Forks ignored on Weaver (single-fork storage); P_NEW works via ReadBuffer */
#ifndef ReadBufferExtended
#define ReadBufferExtended(rel, forkNum, blockNum, mode, strategy) \
	ReadBuffer((rel), (blockNum))
#endif

/* Prefer call-site rewrites with Relation; these macros assume `index` in scope */
#ifndef UnlockReleaseBuffer
#define UnlockReleaseBuffer(buf) \
	do { \
		LockBuffer(index, (buf), BUFFER_LOCK_UNLOCK); \
		ReleaseBuffer(index, (buf)); \
	} while (0)
#endif

#ifndef LockBufferForCleanup
#define LockBufferForCleanup(buf) \
	LockBuffer(index, (buf), BUFFER_LOCK_EXCLUSIVE)
#endif



/* === PGVECTOR_AM_COMPAT_EXTRA === */
#ifndef CHECK_FOR_INTERRUPTS
#define CHECK_FOR_INTERRUPTS() ((void)0)
#endif
#ifndef unlikely
#define unlikely(x) (x)
#endif
#ifndef likely
#define likely(x) (x)
#endif

/*
 * maintenance_work_mem / work_mem are kB budgets in modern PG.
 * Weaver exposes the same unit as SortMem (miscadmin.h / globals.c).
 */
#ifndef maintenance_work_mem
#define maintenance_work_mem SortMem
#endif
#ifndef work_mem
#define work_mem SortMem
#endif

#ifndef MCXT_ALLOC_HUGE
#define MCXT_ALLOC_HUGE 0
#endif
#ifndef MCXT_ALLOC_ZERO
#define MCXT_ALLOC_ZERO 1
#endif

#ifndef SizeOfPageHeaderData
#define SizeOfPageHeaderData sizeof(PageHeaderData)
#endif

#ifndef BAS_BULKREAD
#define BAS_BULKREAD 1
#endif

#ifndef index_form_tuple
#define index_form_tuple index_formtuple
#endif

#ifndef RelationGetNumberOfBlocksInFork
#define RelationGetNumberOfBlocksInFork(rel, fork) RelationGetNumberOfBlocks(rel)
#endif

/* Relation fields missing on Weaver */
#ifndef InvalidOid
/* provided by postgres headers later */
#endif

/* Collation / options not present on RelationData — rewrite via macros on common exprs is hard;
 * provide helper macros used after source patches. */

static inline void *
palloc_extended(Size size, int flags)
{
	void *p = palloc(size);
	if ((flags) & MCXT_ALLOC_ZERO)
		MemSet(p, 0, size);
	return p;
}

static inline void
MemoryContextReset(MemoryContext context)
{
	(void) context;
	/* Weaver has ResetAndDeleteChildren; no-op is enough for compile-first */
}

#undef FunctionCall0Coll
static inline Datum
FunctionCall0Coll(FmgrInfo *flinfo, Oid collation)
{
	FmgrValues	values;
	bool		isNull = false;

	(void) collation;
	return (Datum) (long) fmgr_c(flinfo, &values, &isNull);
}
#undef FunctionCall1Coll
static inline Datum
FunctionCall1Coll(FmgrInfo *flinfo, Oid collation, Datum arg1)
{
	FmgrValues	values;
	bool		isNull = false;

	(void) collation;
	values.data[0] = arg1;
	return (Datum) (long) fmgr_c(flinfo, &values, &isNull);
}

static inline Datum
FunctionCall2Coll(FmgrInfo *flinfo, Oid collation, Datum arg1, Datum arg2)
{
	FmgrValues	values;
	bool		isNull = false;

	(void) collation;
	values.data[0] = arg1;
	values.data[1] = arg2;
	return (Datum) (long) fmgr_c(flinfo, &values, &isNull);
}

static inline BufferAccessStrategy
GetAccessStrategy(int purpose)
{
	(void) purpose;
	return (BufferAccessStrategy) 0;
}

static inline void
FreeAccessStrategy(BufferAccessStrategy strategy)
{
	(void) strategy;
}

static inline void
LockRelationForExtension(Relation rel, LOCKMODE mode)
{
	(void) rel; (void) mode;
}

static inline void
UnlockRelationForExtension(Relation rel, LOCKMODE mode)
{
	(void) rel; (void) mode;
}

static inline void
vacuum_delay_point(void)
{
}

/*
 * Delete multiple index tuples from a page.
 *
 * Callers (e.g. ivfflat bulkdelete) pass itemnos in ascending order. Walk
 * the array from high to low and call PageIndexTupleDelete so earlier
 * offsets remain valid as line pointers are compacted — same strategy
 * upstream uses for the nitems <= 2 fast path.
 */
#include "storage/bufpage.h"

static inline void
PageIndexMultiDelete(Page page, OffsetNumber *itemnos, int nitems)
{
	int			i;

	if (page == NULL || itemnos == NULL || nitems <= 0)
		return;

	for (i = nitems - 1; i >= 0; i--)
		PageIndexTupleDelete(page, itemnos[i]);
}

/*
 * Replace an existing index tuple in place (modern PG API missing from PG7).
 * Same-size overwrite is the common HNSW neighbor-tuple path; larger tuples
 * require free space and relocate preceding item data like upstream.
 */
static inline bool
PageIndexTupleOverwrite(Page page, OffsetNumber offnum, Item item, Size newsize)
{
	PageHeader	phdr = (PageHeader) page;
	ItemId		tupid;
	Size		oldsize;
	Size		alignedold;
	Size		alignednew;
	unsigned	offset;
	int			size_diff;
	int			itemcount;
	int			i;

	if (page == NULL || item == NULL || newsize == 0)
		return false;

	itemcount = PageGetMaxOffsetNumber(page);
	if ((int) offnum <= 0 || (int) offnum > itemcount)
		elog(ERROR, "PageIndexTupleOverwrite: invalid offnum %u", offnum);

	tupid = PageGetItemId(page, offnum);
	if (!(tupid->lp_flags & LP_USED))
		elog(ERROR, "PageIndexTupleOverwrite: unused item %u", offnum);

	oldsize = ItemIdGetLength(tupid);
	offset = ItemIdGetOffset(tupid);
	alignedold = MAXALIGN(oldsize);
	alignednew = MAXALIGN(newsize);

	if (alignednew > alignedold + (phdr->pd_upper - phdr->pd_lower))
		return false;

	size_diff = (int) alignedold - (int) alignednew;
	if (size_diff != 0)
	{
		char	   *addr = (char *) page + phdr->pd_upper;

		memmove(addr + size_diff, addr, offset - phdr->pd_upper);
		phdr->pd_upper += size_diff;
		for (i = FirstOffsetNumber; i <= itemcount; i++)
		{
			ItemId		ii = PageGetItemId(page, i);

			if ((ii->lp_flags & LP_USED) && ItemIdGetOffset(ii) <= offset)
				ii->lp_off += size_diff;
		}
	}

	tupid->lp_off = offset + size_diff;
	tupid->lp_len = newsize;
	memcpy((char *) page + tupid->lp_off, item, newsize);
	return true;
}

static inline Size
PageGetExactFreeSpace(Page page)
{
	return PageGetFreeSpace(page);
}

static inline void
log_newpage_range(Relation rel, ForkNumber fork, BlockNumber start, BlockNumber end, bool page_std)
{
	(void) rel; (void) fork; (void) start; (void) end; (void) page_std;
}

static inline void
pgstat_progress_update_param(int index, int64 val)
{
	(void) index; (void) val;
}

/* Replaces port-stubs/pgstat.h */
static inline void
pgstat_count_index_scan(Relation index)
{
	(void) index;
}

static inline Size
MemoryContextMemAllocated(MemoryContext context, bool recurse)
{
	(void) context;
	(void) recurse;
	return 0;
}

#include "pgvector_index.h"
#include "access/itup.h"

/* Parallel / progress stubs; index build scan in pgvector_executor_port.h */

static inline int
plan_create_index_workers(Oid heapId, Oid indexId)
{
	(void) heapId; (void) indexId;
	return 0;
}

static inline Size
ParallelEstimateShared(Relation heap, void *snapshot)
{
	(void) heap; (void) snapshot;
	return 0;
}

static inline void
table_parallelscan_initialize(Relation heap, void *pscan, void *snapshot)
{
	(void) heap; (void) pscan; (void) snapshot;
}

/* MemoryContext: utils/mcxt.h via postgres.h */

#ifndef GenerationContextCreate
#define GenerationContextCreate(parent, name, blockSize) \
	AllocSetContextCreate((parent), (name), ALLOCSET_DEFAULT_SIZES)
#endif

#ifndef INT64_FORMAT
#define INT64_FORMAT "%lld"
#endif

#ifndef Max
#define Max(x,y) ((x) > (y) ? (x) : (y))
#endif
#ifndef Min
#define Min(x,y) ((x) < (y) ? (x) : (y))
#endif


