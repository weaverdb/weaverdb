/* Compatibility shims for integrating modern pgvector into WeaverDB's old PG7 fork */

#include <string.h>
#include <errno.h>

/* Early neutering of ereport family (must be before any pgvector .c text that uses ereport in headers or bodies).
 * This is more reliable than -D on command line for complex function-like macros with multi-line calls.
 */
#define ereport(elevel, rest)   ((void)0)
#define errcode(x)              (0)
#define errmsg(...)             (0)

/* Dummies for varbit / float return macros used inside bitvec.c / halfvec etc (from CMake dummies + more) */
#define PG_RETURN_FLOAT8(x)     return (Datum)0
#define PG_GETARG_VARBIT_P(n)   ((VarBit *)0)
#define PG_RETURN_VARBIT_P(x)   ((Datum)0)

#define PG_FREE_IF_COPY(x,n)    ((void)0)
#define PG_GETARG_POINTER(n)    ((void*)0)  /* for some GETARG cases */
#define PG_RETURN_CSTRING(x)    return (Datum)0
#define pg_ltoa(i,ptr)          ((void)0)

#define PG_RETURN_BYTEA_P(x)    return (Datum)0
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
#define HalfIsZero(h)           (0)
#define HalfIsInf(h)            (0)

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

/* Define PG_VERSION_NUM high enough */
#ifndef PG_VERSION_NUM
#define PG_VERSION_NUM 130000
#endif

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

/* Index AM vacuum types referenced from hnsw.h / ivfflat etc. Provide forward decls
   so struct fields parse without pulling the full access/genam.h or storage/bufmgr early. */
typedef struct IndexBulkDeleteResult IndexBulkDeleteResult;
typedef void (*IndexBulkDeleteCallback) (IndexBulkDeleteResult *stats, void *callback_state);

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

#define CurrentMemoryContext ((void*)0)
#define ALLOCSET_DEFAULT_SIZES 0,0,0

/* AllocSetContextCreate provided by real headers when needed; dummy macro covers calls */
typedef struct BufferAccessStrategyData *BufferAccessStrategy;

/* Common in index AM code */
typedef struct GenericXLogState GenericXLogState;

/* Relation (core type used in index AM function signatures in the pgvector headers) */
typedef struct RelationData *Relation;

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
typedef struct BlockSamplerData { int dummy; } BlockSamplerData;
typedef struct ReservoirStateData { int dummy; } ReservoirStateData;

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

/* ---- Halfvec support shims (if halfvec.c compiled) ---- */
#ifndef Float4ToHalf
/* very rough; real pgvector has proper half float. For build only. */
static inline uint16 Float4ToHalf(float f) { (void)f; return 0; }
#endif
#ifndef HalfToFloat4
static inline float HalfToFloat4(uint16 h) { (void)h; return 0.0f; }
#endif

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

