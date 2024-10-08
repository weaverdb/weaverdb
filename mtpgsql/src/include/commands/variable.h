/*-------------------------------------------------------------------------
 *
 * variable.h
 *	  global variables
 *
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
/*
 * Headers for handling of 'SET var TO', 'SHOW var' and 'RESET var'
 * statements
 *
 *
 *
 */
#ifndef VARIABLE_H
#define VARIABLE_H 1


typedef struct cost {
/* from cost.h  */
	double 			effective_cache_size;
	Cost 			random_page_cost;
	Cost 			delegated_random_page_cost;
	Cost 			cpu_tuple_cost;
	Cost 			cpu_delegated_tuple_cost;
	Cost 			cpu_index_tuple_cost;
	Cost 			cpu_delegated_index_tuple_cost;
	Cost 			cpu_operator_cost;
        Cost                    thread_startup_cost;
        Cost                    delegation_startup_cost;
	Cost 			disable_cost;
	bool 			enable_seqscan;
	bool 			enable_delegatedseqscan;
	bool 			enable_indexscan;
	bool 			enable_delegatedindexscan;
	bool 			enable_tidscan;
	bool 			enable_sort;
	bool 			enable_nestloop;
	bool 			enable_mergejoin;
	bool			enable_hashjoin;
/* statics */
	bool 			enable_geqo;
	int 			geqo_rels;
} CostInfo;


PG_EXTERN bool SetPGVariable(const char *name, const char *value);
PG_EXTERN bool GetPGVariable(const char *name);
PG_EXTERN bool ResetPGVariable(const char *name);

PG_EXTERN void set_default_datestyle(void);

PG_EXTERN CostInfo* GetCostInfo(void);


#endif	 /* VARIABLE_H */
