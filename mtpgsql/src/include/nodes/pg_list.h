/*-------------------------------------------------------------------------
 *
 * pg_list.h
 *	  POSTGRES generic list package
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: pg_list.h,v 1.1.1.1 2006/08/12 00:22:20 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_LIST_H
#define PG_LIST_H

#include "nodes/nodes.h"

/* ----------------------------------------------------------------
 *						node definitions
 * ----------------------------------------------------------------
 */

/*----------------------
 *		Value node
 *
 * The same Value struct is used for three node types: T_Integer,
 * T_Float, and T_String.  Integral values are actually represented
 * by a machine integer, but both floats and strings are represented
 * as strings.	Using T_Float as the node type simply indicates that
 * the contents of the string look like a valid numeric literal.
 *
 * (Before Postgres 7.0, we used a double to represent T_Float,
 * but that creates loss-of-precision problems when the value is
 * ultimately destined to be converted to NUMERIC.	Since Value nodes
 * are only used in the parsing process, not for runtime data, it's
 * better to use the more general representation.)
 *
 * Note that an integer-looking string will get lexed as T_Float if
 * the value is too large to fit in a 'long'.
 *----------------------
 */
typedef struct Value
{
	NodeTag		type;			/* tag appropriately (eg. T_String) */
	union ValUnion
	{
		long		ival;		/* machine integer */
		char	   *str;		/* string */
	}			val;
} Value;

#define intVal(v)		(((Value *)(v))->val.ival)
#define floatVal(v)		atof(((Value *)(v))->val.str)
#define strVal(v)		(((Value *)(v))->val.str)


/*----------------------
 *		List node
 *----------------------
 */
typedef struct List
{
	NodeTag		type;
	union
	{
		void	   *ptr_value;
		long	    int_value;
	}			elem;
	struct List *next;
} List;

#define    NIL			((List *) NULL)

/* ----------------
 *		accessor macros
 * ----------------
 */

/* anything that doesn't end in 'i' is assumed to be referring to the */
/* pointer version of the list (where it makes a difference)		  */
#define lfirst(l)								((l)->elem.ptr_value)
#define lnext(l)								((l)->next)
#define lsecond(l)								lfirst(lnext(l))

#define lfirsti(l)								((l)->elem.int_value)

/*
 * foreach -
 *	  a convenience macro which loops through the list
 */
#define foreach(_elt_,_list_)	\
	for(_elt_=(_list_); _elt_!=NIL; _elt_=lnext(_elt_))


/*
 * function prototypes in nodes/list.c
 */
 #ifdef __cplusplus
 extern "C" {
 #endif
 
PG_EXTERN long	length(List *list);
PG_EXTERN List *nconc(List *list1, List *list2);
PG_EXTERN List *lcons(void *datum, List *list);
PG_EXTERN List *lconsi(long datum, List *list);
PG_EXTERN bool member(void *datum, List *list);
PG_EXTERN bool intMember(long datum, List *list);
PG_EXTERN Value *makeInteger(long i);
PG_EXTERN Value *makeFloat(char *numericStr);
PG_EXTERN Value *makeString(char *str);
PG_EXTERN List *makeList(void *elem,...);
PG_EXTERN List *lappend(List *list, void *datum);
PG_EXTERN List *lappendi(List *list, long datum);
PG_EXTERN List *lremove(void *elem, List *list);
PG_EXTERN List *LispRemove(void *elem, List *list);
PG_EXTERN List *ltruncate(long n, List *list);

PG_EXTERN void *nth(long n, List *l);
PG_EXTERN long	nthi(long n, List *l);
PG_EXTERN void set_nth(List *l, long n, void *elem);

PG_EXTERN List *set_difference(List *list1, List *list2);
PG_EXTERN List *set_differencei(List *list1, List *list2);
PG_EXTERN List *LispUnion(List *list1, List *list2);
PG_EXTERN List *LispUnioni(List *list1, List *list2);

PG_EXTERN bool sameseti(List *list1, List *list2);
PG_EXTERN bool nonoverlap_setsi(List *list1, List *list2);
PG_EXTERN bool is_subseti(List *list1, List *list2);

PG_EXTERN void freeList(List *list);

/* should be in nodes.h but needs List */

/* in copyfuncs.c */
PG_EXTERN List *listCopy(List *list);
 #ifdef __cplusplus
}
 #endif

#endif	 /* PG_LIST_H */
