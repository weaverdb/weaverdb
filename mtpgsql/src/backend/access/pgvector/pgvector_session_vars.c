/*
 * Runtime search knobs for pgvector on WeaverDB.
 *
 * Modern GUC (DefineCustom*) is unavailable in this PG7 fork. Instead, expose
 * knobs via SET/SHOW/RESET into the existing per-thread env structs that
 * hnswscan/ivfscan/pgvector_cost already read.
 */

#include "postgres.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "access/pgvector_session_vars.h"
#include "hnsw.h"
#include "ivfflat.h"
#include "utils/builtins.h"

#include <limits.h>

static bool
parse_int_range(char *value, int *dest, int minv, int maxv, const char *name)
{
	int			v;

	if (value == NULL)
		return FALSE;

	v = pg_atoi(value, sizeof(int), '\0');
	if (v < minv || v > maxv)
		elog(ERROR, "Bad value for %s (%s)", name, value);
	*dest = v;
	return TRUE;
}

static bool
parse_double_range(char *value, double *dest, double minv, double maxv,
				   const char *name)
{
	double		v;
	char	   *end;

	if (value == NULL)
		return FALSE;

	v = strtod(value, &end);
	if (end == value || (end != NULL && *end != '\0') ||
		v < minv || v > maxv || isnan(v))
		elog(ERROR, "Bad value for %s (%s)", name, value);
	*dest = v;
	return TRUE;
}

static int
parse_scan_mode(char *value, bool allow_strict, const char *name)
{
	char		buf[64];
	size_t		i;
	size_t		n;

	if (value == NULL)
		elog(ERROR, "Bad value for %s (NULL)", name);

	n = strlen(value);
	if (n >= sizeof(buf))
		elog(ERROR, "Bad value for %s (%s)", name, value);
	for (i = 0; i < n; i++)
		buf[i] = (char) tolower((unsigned char) value[i]);
	buf[n] = '\0';

	if (strcmp(buf, "off") == 0)
		return 0;				/* OFF for both AMs */
	if (strcmp(buf, "relaxed_order") == 0 || strcmp(buf, "relaxed") == 0)
		return 1;				/* RELAXED */
	if (allow_strict &&
		(strcmp(buf, "strict_order") == 0 || strcmp(buf, "strict") == 0))
		return 2;				/* STRICT (hnsw only) */

	elog(ERROR, "Bad value for %s (%s)", name, value);
	return 0;
}

static const char *
hnsw_scan_mode_name(int mode)
{
	switch (mode)
	{
		case HNSW_ITERATIVE_SCAN_RELAXED:
			return "relaxed_order";
		case HNSW_ITERATIVE_SCAN_STRICT:
			return "strict_order";
		case HNSW_ITERATIVE_SCAN_OFF:
		default:
			return "off";
	}
}

static const char *
ivfflat_scan_mode_name(int mode)
{
	switch (mode)
	{
		case IVFFLAT_ITERATIVE_SCAN_RELAXED:
			return "relaxed_order";
		case IVFFLAT_ITERATIVE_SCAN_OFF:
		default:
			return "off";
	}
}

/* ---- hnsw.ef_search ---- */

bool
parse_hnsw_ef_search(char *value)
{
	if (value == NULL)
		return reset_hnsw_ef_search();
	return parse_int_range(value, &HnswGetEnv()->ef_search,
						   HNSW_MIN_EF_SEARCH, HNSW_MAX_EF_SEARCH,
						   "hnsw.ef_search");
}

bool
show_hnsw_ef_search(void)
{
	elog(NOTICE, "hnsw.ef_search is %d", HnswGetEnv()->ef_search);
	return TRUE;
}

bool
reset_hnsw_ef_search(void)
{
	HnswGetEnv()->ef_search = HNSW_DEFAULT_EF_SEARCH;
	return TRUE;
}

/* ---- hnsw.iterative_scan ---- */

bool
parse_hnsw_iterative_scan(char *value)
{
	if (value == NULL)
		return reset_hnsw_iterative_scan();
	HnswGetEnv()->iterative_scan =
		parse_scan_mode(value, true, "hnsw.iterative_scan");
	return TRUE;
}

bool
show_hnsw_iterative_scan(void)
{
	elog(NOTICE, "hnsw.iterative_scan is %s",
		 hnsw_scan_mode_name(HnswGetEnv()->iterative_scan));
	return TRUE;
}

bool
reset_hnsw_iterative_scan(void)
{
	HnswGetEnv()->iterative_scan = HNSW_ITERATIVE_SCAN_OFF;
	return TRUE;
}

/* ---- hnsw.max_scan_tuples ---- */

bool
parse_hnsw_max_scan_tuples(char *value)
{
	if (value == NULL)
		return reset_hnsw_max_scan_tuples();
	return parse_int_range(value, &HnswGetEnv()->max_scan_tuples,
						   1, INT_MAX, "hnsw.max_scan_tuples");
}

bool
show_hnsw_max_scan_tuples(void)
{
	elog(NOTICE, "hnsw.max_scan_tuples is %d",
		 HnswGetEnv()->max_scan_tuples);
	return TRUE;
}

bool
reset_hnsw_max_scan_tuples(void)
{
	HnswGetEnv()->max_scan_tuples = 20000;
	return TRUE;
}

/* ---- hnsw.scan_mem_multiplier ---- */

bool
parse_hnsw_scan_mem_multiplier(char *value)
{
	if (value == NULL)
		return reset_hnsw_scan_mem_multiplier();
	return parse_double_range(value, &HnswGetEnv()->scan_mem_multiplier,
							  1.0, 1000.0, "hnsw.scan_mem_multiplier");
}

bool
show_hnsw_scan_mem_multiplier(void)
{
	elog(NOTICE, "hnsw.scan_mem_multiplier is %g",
		 HnswGetEnv()->scan_mem_multiplier);
	return TRUE;
}

bool
reset_hnsw_scan_mem_multiplier(void)
{
	HnswGetEnv()->scan_mem_multiplier = 1.0;
	return TRUE;
}

/* ---- ivfflat.probes ---- */

bool
parse_ivfflat_probes(char *value)
{
	if (value == NULL)
		return reset_ivfflat_probes();
	return parse_int_range(value, &IvfflatGetEnv()->probes,
						   IVFFLAT_MIN_LISTS, IVFFLAT_MAX_LISTS,
						   "ivfflat.probes");
}

bool
show_ivfflat_probes(void)
{
	elog(NOTICE, "ivfflat.probes is %d", IvfflatGetEnv()->probes);
	return TRUE;
}

bool
reset_ivfflat_probes(void)
{
	IvfflatGetEnv()->probes = IVFFLAT_DEFAULT_PROBES;
	return TRUE;
}

/* ---- ivfflat.iterative_scan ---- */

bool
parse_ivfflat_iterative_scan(char *value)
{
	if (value == NULL)
		return reset_ivfflat_iterative_scan();
	IvfflatGetEnv()->iterative_scan =
		parse_scan_mode(value, false, "ivfflat.iterative_scan");
	return TRUE;
}

bool
show_ivfflat_iterative_scan(void)
{
	elog(NOTICE, "ivfflat.iterative_scan is %s",
		 ivfflat_scan_mode_name(IvfflatGetEnv()->iterative_scan));
	return TRUE;
}

bool
reset_ivfflat_iterative_scan(void)
{
	IvfflatGetEnv()->iterative_scan = IVFFLAT_ITERATIVE_SCAN_OFF;
	return TRUE;
}

/* ---- ivfflat.max_probes ---- */

bool
parse_ivfflat_max_probes(char *value)
{
	if (value == NULL)
		return reset_ivfflat_max_probes();
	return parse_int_range(value, &IvfflatGetEnv()->max_probes,
						   IVFFLAT_MIN_LISTS, IVFFLAT_MAX_LISTS,
						   "ivfflat.max_probes");
}

bool
show_ivfflat_max_probes(void)
{
	elog(NOTICE, "ivfflat.max_probes is %d", IvfflatGetEnv()->max_probes);
	return TRUE;
}

bool
reset_ivfflat_max_probes(void)
{
	IvfflatGetEnv()->max_probes = IVFFLAT_DEFAULT_LISTS;
	return TRUE;
}
