/*-------------------------------------------------------------------------
 *
 * parse_node.h
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef PARSE_NODE_H
#define PARSE_NODE_H

#include "nodes/parsenodes.h"
#include "utils/rel.h"

/* State information used during parse analysis
 * p_join_quals is a list of untransformed qualification expressions
 * (implicitly ANDed together) found in the FROM clause.
 * Needs to be available later to merge with other qualifiers from the
 * WHERE clause.
 */
typedef struct ParseState
{
	int			p_last_resno;
	List	   *p_rtable;
	struct ParseState *parentParseState;
	bool		p_hasAggs;
	bool		p_hasSubLinks;
	bool		p_is_insert;
	bool		p_is_update;
	bool		p_is_rule;
	bool		p_in_where_clause;
	Relation	p_target_relation;
	RangeTblEntry *p_target_rangetblentry;
	List	   *p_shape;
	List	   *p_alias;
	List	   *p_join_quals;
} ParseState;

PG_EXTERN ParseState *make_parsestate(ParseState *parentParseState);
PG_EXTERN Expr *make_op(char *opname, Node *ltree, Node *rtree);
PG_EXTERN Node *make_operand(char *opname, Node *tree,
			 Oid orig_typeId, Oid target_typeId);
PG_EXTERN Var *make_var(ParseState *pstate, Oid relid, char *refname,
		 char *attrname);
PG_EXTERN ArrayRef *transformArraySubscripts(ParseState *pstate,
						 Node *arrayBase,
						 List *indirection,
						 bool forceSlice,
						 Node *assignFrom);
PG_EXTERN Const *make_const(Value *value);

#endif	 /* PARSE_NODE_H */
