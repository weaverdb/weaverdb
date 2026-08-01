/* Compatibility shims for integrating modern pgvector into WeaverDB's old PG7 fork */

#include <string.h>
#include <errno.h>

/* Early neutering of ereport family (must be before any pgvector .c text that uses ereport in headers or bodies).
 * This is more reliable than -D on command line for complex function-like macros with multi-line calls.
 */
#define ereport(elevel, rest)   ((void)0)
#define errcode(x)              (0)
#define errmsg(...)             (0)

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
#define pq_sendfloat4(buf,f)    ((void)0)
#define pq_endtypsend(buf)      ((void*)0)
#define pq_begin_typsend()      ((StringInfo)0)

#define PG_RETURN_INT32(x)      return (Datum)0
#define pq_getmsgint(buf,sz)    (0)
#define pq_getmsgfloat4(buf)    (0.0f)
#define pq_begintypsend(buf)    ((void)0)

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

/* Common missing base types/macros used by pgvector and shims (hoisted early, lightweight) */
#ifndef Size
typedef size_t Size;
#endif

#ifndef Datum
typedef unsigned long Datum;   /* common definition; project/c.h will provide equivalent later */
#endif

#ifndef FLOAT8PASSBYVAL
#define FLOAT8PASSBYVAL true
#endif

#ifndef TYPALIGN_INT
#define TYPALIGN_INT 'i'
#endif
#ifndef TYPALIGN_DOUBLE
#define TYPALIGN_DOUBLE 'd'
#endif

#ifndef ERRCODE_FEATURE_NOT_SUPPORTED
#define ERRCODE_FEATURE_NOT_SUPPORTED 0
#endif
#ifndef DEBUG1
#define DEBUG1 10
#endif

/* Forward declare so VarBit name and ArrayType (from real headers) can be used in prototypes
   without pulling heavy includes at -include time (which happens before postgres.h in the .c). */
struct varbita;
typedef struct varbita VarBit;

/* Index AM vacuum types - forward decls only here (full defs after postgres.h) */
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

/* Progress and optimizer types for cost/ build */
#define PROGRESS_CREATEIDX_SUBPHASE_INITIALIZE 0
typedef struct PlannerInfo PlannerInfo;
typedef struct IndexPath IndexPath;
typedef struct RelOptInfo RelOptInfo;

/* CurrentMemoryContext provided by real headers */
#ifndef CurrentMemoryContext
#define CurrentMemoryContext (MemoryContextGetTopContext())
#endif
#define ALLOCSET_DEFAULT_SIZES ALLOCSET_DEFAULT_MINSIZE, ALLOCSET_DEFAULT_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE

/* AllocSetContextCreate provided by real headers when needed; dummy macro covers calls */
typedef struct BufferAccessStrategyData *BufferAccessStrategy;

/* itemptr callback — full ItemPointer after postgres.h; use void* here */
typedef bool (*IndexBulkDeleteCallback) (void *itemptr, void *callback_state);

/* Other frequently referenced in index build/scan/vacuum headers */
typedef struct List List;
typedef char *Page;          /* simplified; real is Page (pointer to bufpage) */
/* Buffer / BlockNumber intentionally not typedef'd here to avoid signedness/width clashes
   with the project's definitions in storage/buf.h and storage/block.h (pulled via postgres.h). */

/* DSM / shared memory / index build result types (for parallel index and am build/ vacuum hooks in hnsw/ivf headers) */
typedef struct dsm_segment dsm_segment;
typedef struct shm_toc shm_toc;

typedef struct IndexBuildResult IndexBuildResult;
typedef int IndexUniqueCheck;
typedef struct IndexVacuumInfo IndexVacuumInfo;

typedef struct Sharedsort { int dummy; } Sharedsort;

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

/* For atomics if not */
#ifndef pg_atomic_uint32
/* already in our atomics stub */
#endif

/* popcount table is provided in our pg_bitutils stub */

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

#undef ereport
#undef errcode
#undef errmsg
#undef errdetail
#define errcode(code) 0
#define errmsg(...) ""
#define errdetail(...) ""
#define ereport(elevel, rest) \
	do { \
		int _pgv_ereport_level = (elevel); \
		if (_pgv_ereport_level == FATAL || _pgv_ereport_level == REALLYFATAL) \
			elog(FATAL, "pgvector: operation failed"); \
		else if (_pgv_ereport_level == ERROR) \
			elog(ERROR, "pgvector: operation failed"); \
		else if (_pgv_ereport_level >= NOTICE) \
			elog(NOTICE, "pgvector notice"); \
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

/* IS_NOT_ZERO helper seen in sparsevec */
#ifndef IS_NOT_ZERO
#define IS_NOT_ZERO(f) ((f) != 0.0f)
#endif

/* For index AM relation options etc that may pull in */
#ifndef RELOPT_KIND_IVFFLAT
#define RELOPT_KIND_IVFFLAT  (1 << 10)   /* arbitrary high bit */
#endif
#ifndef RELOPT_KIND_HNSW
#define RELOPT_KIND_HNSW     (1 << 11)
#endif

/* elog/ereport already provided by elog.h via postgres.h */

/* end of pgvector_compat.h shims */

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

#ifndef MarkBufferDirty
#define MarkBufferDirty(buf) ((void) (buf))
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

#ifndef maintenance_work_mem
#define maintenance_work_mem (1024)
#endif
#ifndef work_mem
#define work_mem maintenance_work_mem
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

static inline void
PageIndexMultiDelete(Page page, OffsetNumber *itemnos, int nitems)
{
	(void) page; (void) itemnos; (void) nitems;
}

static inline bool
PageIndexTupleOverwrite(Page page, OffsetNumber off, Item item, Size size)
{
	(void) page; (void) off; (void) item; (void) size;
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

typedef struct MemoryContextData *MemoryContext; /* may already exist */

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


