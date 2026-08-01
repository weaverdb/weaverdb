/* pgvector_shims.c
 *
 * Build-system glue for integrating pgvector sources into WeaverDB.
 * Provides:
 *   - implementations of deconstruct_array / construct_array (minimal, 1-d float/int focused)
 *   - numeric_float4 (simple cast via float8in/float4out or direct)
 *   - get_float8_infinity (already inline in header but can be here too)
 *   - any other link-time symbols the pgvector/*.c reference that the old fork lacks.
 *
 * This file is *not* an attempt to fully port semantics; it lets the build
 * succeed first. Full fidelity (esp. for chunked arrays, toast, numeric
 * precision, halfvec f16) can be improved later.
 */

#include "postgres.h"
#include "fmgr.h"
#include "utils/array.h"
#include "utils/lsyscache.h"   /* for get_typlenbyvalalign if it exists */
#include "utils/palloc.h"      /* ensure palloc0 visible even if postgres.h chain differs */
#include "catalog/pg_type.h"
#include "lib/stringinfo.h"    /* in case */
#include "utils/varbit.h"
#include "bitvec.h"
#include "hnsw.h"
#include "ivfflat.h"
#include "halfutils.h"
#include "bitutils.h"
#include <math.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>

#include "utils/elog.h"

static pthread_once_t pgvector_module_once = PTHREAD_ONCE_INIT;

static void
pgvector_module_init_body(void)
{
	HalfvecInit();
	BitvecInit();
	HnswInit();
	IvfflatInit();
}

static void
pgvector_module_init_once(void)
{
	pgvector_module_init_body();
}

void
PgvectorModuleInit(void)
{
	pthread_once(&pgvector_module_once, pgvector_module_init_once);
}

/* ----------------------------------------------------------------
 * Minimal deconstruct_array / construct_array for pgvector's needs.
 *
 * Only supports simple 1-dimensional arrays of int4/float4/float8
 * (and falls back for numeric by returning 0s). It walks the
 * project's ArrayType layout using ARR_DIMS / ARR_DATA_PTR and
 * produces a palloc'ed Datum[] of *values* (by-val style so that
 * DatumGetFloat4 etc in pgvector code see the bits in the Datum word).
 *
 * This makes array_to_vector('{1.0,2.0}'::float4[]) and vector_to_float4
 * work for the common case.
 * ---------------------------------------------------------------- */

void
deconstruct_array(ArrayType *array,
				  Oid elmtype, int elmlen, bool elmbyval, char elmalign,
				  Datum **elemsp, bool **nullsp, int *nelemsp)
{
	int			ndim;
	int			nitems;
	int			i;
	char	   *p;
	Datum	   *out;

	if (array == NULL)
	{
		*elemsp = NULL;
		if (nullsp)
			*nullsp = NULL;
		*nelemsp = 0;
		return;
	}

	ndim = ARR_NDIM(array);
	if (ndim != 1)
	{
		/* pgvector code already errors on >1; we still compute nitems=0 */
		nitems = 0;
	}
	else
	{
		nitems = ARR_DIMS(array)[0];
	}

	out = (Datum *) palloc0(sizeof(Datum) * nitems);

	/* Walk data - project stores raw element bytes after dims+lb */
	p = (char *) ARR_DATA_PTR(array);

	for (i = 0; i < nitems; i++)
	{
		if (elmtype == INT4OID)
		{
			int32 v = *(int32 *) p;
			out[i] = Int32GetDatum(v);
			p += sizeof(int32);
		}
		else if (elmtype == FLOAT4OID)
		{
			float4 v = *(float4 *) p;
			/* store so that DatumGetFloat4(out[i]) will recover it */
			memcpy(&out[i], &v, sizeof(float4));   /* low bytes of the Datum word */
			p += sizeof(float4);
		}
		else if (elmtype == FLOAT8OID)
		{
			float8 v = *(float8 *) p;
			memcpy(&out[i], &v, sizeof(float8));
			p += sizeof(float8);
		}
		else
		{
			/* numeric or other: leave 0, caller will see 0s or error upstream */
			out[i] = Int32GetDatum(0);
			/* best effort advance; if varlena we can't really without typlen */
			p += (elmlen > 0 ? elmlen : sizeof(Datum));
		}
	}

	*elemsp = out;
	if (nullsp)
		*nullsp = NULL;   /* we never report nulls in this shim */
	*nelemsp = nitems;
}

ArrayType *
construct_array(Datum *elems, int nelems,
				Oid elmtype, int elmlen, bool elmbyval, char elmalign)
{
	ArrayType  *result;
	int			nbytes;
	char	   *p;
	int			i;

	if (nelems < 0)
		nelems = 0;

	/* Compute total size similar to project's array overhead + data */
	nbytes = ARR_OVERHEAD(1) + (nelems * (elmlen > 0 ? elmlen : sizeof(Datum)));

	result = (ArrayType *) palloc0(nbytes);
	result->size = nbytes;
	result->ndim = 1;
	result->flags = 0;   /* not lob, not chunked for this purpose */

	/* dims and lbound live right after the fixed header in project layout */
	{
		int *dims = (int *) (((char *) result) + sizeof(ArrayType));
		int *lb   = dims + 1;
		dims[0] = nelems;
		lb[0]   = 1;   /* default */
	}

	p = (char *) ARR_DATA_PTR(result);

	for (i = 0; i < nelems; i++)
	{
		if (elmtype == INT4OID)
		{
			int32 v = DatumGetInt32(elems[i]);
			*(int32 *) p = v;
			p += sizeof(int32);
		}
		else if (elmtype == FLOAT4OID)
		{
			float4 v;
			memcpy(&v, &elems[i], sizeof(float4));
			*(float4 *) p = v;
			p += sizeof(float4);
		}
		else if (elmtype == FLOAT8OID)
		{
			float8 v;
			memcpy(&v, &elems[i], sizeof(float8));
			*(float8 *) p = v;
			p += sizeof(float8);
		}
		else
		{
			/* best effort */
			if (elmlen > 0)
			{
				memcpy(p, &elems[i], elmlen);
				p += elmlen;
			}
			else
			{
				/* varlena style - just copy the datum as pointer-ish */
				*(Datum *) p = elems[i];
				p += sizeof(Datum);
			}
		}
	}

	return result;
}

/* ----------------------------------------------------------------
 * Additional small shims that may be referenced
 * ---------------------------------------------------------------- */

/* Some index build code calls these; provide no-op or basic versions */
void
_PG_init(void);   /* already defined in vector.c - do not duplicate */

/* Bodies for the small array helpers declared in force-included compat.h */
bool
array_contains_nulls(ArrayType *array)
{
	(void) array;
	return false;
}

void
get_typlenbyvalalign(Oid elemtype, int16 *typlen, bool *typbyval, char *typalign)
{
	if (elemtype == INT4OID)
	{
		*typlen = sizeof(int32);
		*typbyval = true;
		*typalign = TYPALIGN_INT;
	}
	else if (elemtype == FLOAT4OID)
	{
		*typlen = sizeof(float4);
		*typbyval = true;
		*typalign = TYPALIGN_INT;
	}
	else if (elemtype == FLOAT8OID)
	{
		*typlen = sizeof(float8);
		*typbyval = FLOAT8PASSBYVAL;
		*typalign = TYPALIGN_DOUBLE;
	}
	else
	{
		*typlen = -1;
		*typbyval = false;
		*typalign = TYPALIGN_INT;
	}
}

/* Datum <-> float converters (prototypes in compat.h). Store bits in Datum low bytes. */
float4
DatumGetFloat4(Datum X)
{
	float4 f;
	memcpy(&f, &X, sizeof(float4));
	return f;
}

Datum
Float4GetDatum(float4 X)
{
	Datum d = 0;
	memcpy(&d, &X, sizeof(float4));
	return d;
}

float8
DatumGetFloat8(Datum X)
{
	if (X == (Datum) 0)
		return (float8) 0;
	return *((float8 *) (long) X);
}

Datum
Float8GetDatum(float8 X)
{
	Datum d = 0;
	memcpy(&d, &X, sizeof(float8));
	return d;
}

/* ----------------------------------------------------------------
 * ereport → elog bridge (see port-stubs/pgvector_compat.h)
 *
 * Modern pgvector uses ereport(ERROR, (errcode(...), errmsg(...))).
 * Weaver only has elog/coded_elog. Capture aux fields in TLS, then emit.
 * ---------------------------------------------------------------- */

#define PGVECTOR_ERR_MSG_LEN	1024
#define PGVECTOR_ERR_AUX_LEN	512

typedef struct PgvectorErrState
{
	int			sqlerrcode;
	char		message[PGVECTOR_ERR_MSG_LEN];
	char		detail[PGVECTOR_ERR_AUX_LEN];
	char		hint[PGVECTOR_ERR_AUX_LEN];
} PgvectorErrState;

static __thread PgvectorErrState pgvector_err_state;

void
pgvector_err_reset(void)
{
	pgvector_err_state.sqlerrcode = 0;
	pgvector_err_state.message[0] = '\0';
	pgvector_err_state.detail[0] = '\0';
	pgvector_err_state.hint[0] = '\0';
}

int
pgvector_errcode(int sqlerrcode)
{
	pgvector_err_state.sqlerrcode = sqlerrcode;
	return 0;
}

static void
pgvector_err_vset(char *buf, size_t buflen, const char *fmt, va_list ap)
{
	if (buf == NULL || buflen == 0)
		return;
	vsnprintf(buf, buflen, fmt, ap);
	buf[buflen - 1] = '\0';
}

int
pgvector_errmsg(const char *fmt,...)
{
	va_list		ap;

	va_start(ap, fmt);
	pgvector_err_vset(pgvector_err_state.message, sizeof(pgvector_err_state.message),
					  fmt, ap);
	va_end(ap);
	return 0;
}

int
pgvector_errdetail(const char *fmt,...)
{
	va_list		ap;

	va_start(ap, fmt);
	pgvector_err_vset(pgvector_err_state.detail, sizeof(pgvector_err_state.detail),
					  fmt, ap);
	va_end(ap);
	return 0;
}

int
pgvector_errhint(const char *fmt,...)
{
	va_list		ap;

	va_start(ap, fmt);
	pgvector_err_vset(pgvector_err_state.hint, sizeof(pgvector_err_state.hint),
					  fmt, ap);
	va_end(ap);
	return 0;
}

static int
pgvector_map_elevel(int elevel)
{
	if (elevel == ERROR)
		return ERROR;
	if (elevel == FATAL)
		return FATAL;
	if (elevel == REALLYFATAL || elevel == STOP)
		return REALLYFATAL;
	if (elevel == DEBUG || elevel == NOIND || elevel == LOG)
		return DEBUG;
	if (elevel == NOTICE)
		return NOTICE;
	/* PG13+ DEBUG1.. and other positive modern levels (DEBUG1=10 here) */
	if (elevel >= 10)
		return DEBUG;
	return NOTICE;
}

void
pgvector_ereport(int elevel)
{
	int			lev = pgvector_map_elevel(elevel);
	char		full[PGVECTOR_ERR_MSG_LEN + 2 * PGVECTOR_ERR_AUX_LEN + 64];
	const char *msg = pgvector_err_state.message[0] ?
		pgvector_err_state.message : "pgvector error";
	size_t		n;

	n = snprintf(full, sizeof(full), "%s", msg);
	if (n < sizeof(full) && pgvector_err_state.detail[0])
		n += snprintf(full + n, sizeof(full) - n, "\nDETAIL:  %s",
					  pgvector_err_state.detail);
	if (n < sizeof(full) && pgvector_err_state.hint[0])
		snprintf(full + n, sizeof(full) - n, "\nHINT:  %s",
				 pgvector_err_state.hint);

	if (lev == ERROR || lev == FATAL || lev == REALLYFATAL)
	{
		int			code = pgvector_err_state.sqlerrcode;

		if (code != 0)
			coded_elog(lev, code, "%s", full);
		else
			elog(lev, "%s", full);
	}
	else
		elog(lev, "%s", full);
}

char *
pgvector_pnstrdup(const char *in, size_t len)
{
	char	   *out;

	if (in == NULL)
		return NULL;
	out = (char *) palloc(len + 1);
	memcpy(out, in, len);
	out[len] = '\0';
	return out;
}

/* palloc0 implementation for all the pgvector sources that call it.
 * Placed here so the symbol is provided exactly once (shims is always in the OBJECT lib).
 */
void *
palloc0(Size size)
{
	void *p = palloc(size);
	if (p != NULL)
		memset(p, 0, size);
	return p;
}

#ifdef NOT_USED
/* If any .c directly calls these without fmgr, provide here */
#endif

/* end pgvector_shims.c */
