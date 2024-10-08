 /*-------------------------------------------------------------------------
 *
 * freefuncs.c
 *	  Free functions for Postgres tree nodes.
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "env/env.h"
#include "optimizer/planmain.h"
#include "access/heapam.h"

/* ****************************************************************
 *					 plannodes.h free functions
 * ****************************************************************
 */
static void freeObject(void *obj);

/* ----------------
 *		FreePlanFields
 *
 *		This function frees the fields of the Plan node.  It is used by
 *		all the free functions for classes which inherit node Plan.
 * ----------------
 */
static void
FreePlanFields(Plan *node)
{
	freeObject(node->targetlist);
	freeObject(node->qual);
	freeObject(node->lefttree);
	freeObject(node->righttree);
	freeList(node->extParam);
	freeList(node->locParam);
	freeList(node->chgParam);
	freeObject(node->initPlan);
	freeList(node->subPlan);
}

/* ----------------
 *		_freePlan
 * ----------------
 */
static void
_freePlan(Plan *node)
{
	/* ----------------
	 *	free the node superclass fields
	 * ----------------
	 */
	FreePlanFields(node);

	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	pfree(node);
}


/* ----------------
 *		_freeResult
 * ----------------
 */
static void
_freeResult(Result *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);

	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeObject(node->resconstantqual);

	pfree(node);
}

/* ----------------
 *		_freeAppend
 * ----------------
 */
static void
_freeAppend(Append *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);

	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeObject(node->appendplans);
	freeObject(node->unionrtables);
	freeObject(node->inheritrtable);

	pfree(node);
}


/* ----------------
 *		FreeScanFields
 *
 *		This function frees the fields of the Scan node.  It is used by
 *		all the free functions for classes which inherit node Scan.
 * ----------------
 */
static void
FreeScanFields(Scan *node)
{
}

/* ----------------
 *		_freeScan
 * ----------------
 */
static void
_freeScan(Scan *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);
	FreeScanFields((Scan *) node);

	pfree(node);
}

/* ----------------
 *		_freeSeqScan
 * ----------------
 */
static void
_freeSeqScan(SeqScan *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);
	FreeScanFields((Scan *) node);

	pfree(node);
}

/* ----------------
 *		_freeDelegatedSeqScan
 * ----------------
 */
static void
_freeDelegatedSeqScan(DelegatedSeqScan *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);
	FreeScanFields((Scan *) node);
        elog(ERROR,"check _freeDelegatedSeqScan implementation");
	pfree(node);
}

/* ----------------
 *		_freeIndexScan
 * ----------------
 */
static void
_freeIndexScan(IndexScan *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);
	FreeScanFields((Scan *) node);

	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeList(node->indxid);
	freeObject(node->indxqual);
	freeObject(node->indxqualorig);

	pfree(node);
}

static void
_freeDelegatedIndexScan(DelegatedIndexScan *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);
	FreeScanFields((Scan *) node);

	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */

	freeObject(node->indxqual);
	freeObject(node->indxqualorig);

	pfree(node);
}
/* ----------------
 *		_freeTidScan
 * ----------------
 */
static void
_freeTidScan(TidScan *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);
	FreeScanFields((Scan *) node);

	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeObject(node->tideval);

	pfree(node);
}

/* ----------------
 *		FreeJoinFields
 *
 *		This function frees the fields of the Join node.  It is used by
 *		all the free functions for classes which inherit node Join.
 * ----------------
 */
static void
FreeJoinFields(Join *node)
{
	/* nothing extra */
	return;
}


/* ----------------
 *		_freeJoin
 * ----------------
 */
static void
_freeJoin(Join *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);
	FreeJoinFields(node);

	pfree(node);
}


/* ----------------
 *		_freeNestLoop
 * ----------------
 */
static void
_freeNestLoop(NestLoop *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);
	FreeJoinFields((Join *) node);

	pfree(node);
}


/* ----------------
 *		_freeMergeJoin
 * ----------------
 */
static void
_freeMergeJoin(MergeJoin *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);
	FreeJoinFields((Join *) node);

	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeObject(node->mergeclauses);

	pfree(node);
}

/* ----------------
 *		_freeHashJoin
 * ----------------
 */
static void
_freeHashJoin(HashJoin *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);
	FreeJoinFields((Join *) node);

	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeObject(node->hashclauses);

	pfree(node);
}


/* ----------------
 *		FreeNonameFields
 *
 *		This function frees the fields of the Noname node.	It is used by
 *		all the free functions for classes which inherit node Noname.
 * ----------------
 */
static void
FreeNonameFields(Noname *node)
{
	return;
}


/* ----------------
 *		_freeNoname
 * ----------------
 */
static void
_freeNoname(Noname *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);
	FreeNonameFields(node);

	pfree(node);
}

/* ----------------
 *		_freeMaterial
 * ----------------
 */
static void
_freeMaterial(Material *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);
	FreeNonameFields((Noname *) node);

	pfree(node);
}


/* ----------------
 *		_freeSort
 * ----------------
 */
static void
_freeSort(Sort *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);
	FreeNonameFields((Noname *) node);

	pfree(node);
}


/* ----------------
 *		_freeGroup
 * ----------------
 */
static void
_freeGroup(Group *node)
{
	FreePlanFields((Plan *) node);

	pfree(node->grpColIdx);

	pfree(node);
}

/* ---------------
 *	_freeAgg
 * --------------
 */
static void
_freeAgg(Agg *node)
{
	FreePlanFields((Plan *) node);

	pfree(node);
}

/* ---------------
 *	_freeGroupClause
 * --------------
 */
static void
_freeGroupClause(GroupClause *node)
{
	pfree(node);
}


/* ----------------
 *		_freeUnique
 * ----------------
 */
static void
_freeUnique(Unique *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);
	FreeNonameFields((Noname *) node);

	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	pfree(node->uniqColIdx);

	pfree(node);
}


/* ----------------
 *		_freeHash
 * ----------------
 */
static void
_freeHash(Hash *node)
{
	/* ----------------
	 *	free node superclass fields
	 * ----------------
	 */
	FreePlanFields((Plan *) node);

	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeObject(node->hashkey);

	pfree(node);
}

static void
_freeSubPlan(SubPlan *node)
{
	freeObject(node->plan);
	freeObject(node->rtable);
	freeList(node->setParam);
	freeList(node->parParam);
	freeObject(node->sublink);

	if (node->curTuple)
		heap_freetuple(node->curTuple);

	pfree(node);
}

/* ****************************************************************
 *					   primnodes.h free functions
 * ****************************************************************
 */

/* ----------------
 *		_freeResdom
 * ----------------
 */
static void
_freeResdom(Resdom *node)
{
	if (node->resname != NULL)
		pfree(node->resname);

	pfree(node);
}

static void
_freeFjoin(Fjoin *node)
{
	freeObject(node->fj_innerNode);
	pfree(node->fj_results);
	pfree(node->fj_alwaysDone);

	pfree(node);
}

/* ----------------
 *		_freeExpr
 * ----------------
 */
static void
_freeExpr(Expr *node)
{
	freeObject(node->oper);
	freeObject(node->args);

	pfree(node);
}

/* ----------------
 *		_freeVar
 * ----------------
 */
static void
_freeVar(Var *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	pfree(node);
}

static void
_freeFcache(FunctionCachePtr ptr)
{
	if (ptr->argOidVect)
		pfree(ptr->argOidVect);
	if (ptr->nullVect)
		pfree(ptr->nullVect);
	if (ptr->src)
		pfree(ptr->src);
	if (ptr->bin)
		pfree(ptr->bin);
	if (ptr->func_state)
		pfree(ptr->func_state);
	if (ptr->setArg)
		pfree(ptr->setArg);

	pfree(ptr);
}

/* ----------------
 *		_freeOper
 * ----------------
 */
static void
_freeOper(Oper *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	if (node->op_fcache)
		_freeFcache(node->op_fcache);

	pfree(node);
}

/* ----------------
 *		_freeConst
 * ----------------
 */
static void
_freeConst(Const *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	if (!node->constbyval)
		pfree((void *) node->constvalue);

	pfree(node);
}

/* ----------------
 *		_freeParam
 * ----------------
 */
static void
_freeParam(Param *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	if (node->paramname != NULL)
		pfree(node->paramname);
	freeObject(node->param_tlist);

	pfree(node);
}

/* ----------------
 *		_freeFunc
 * ----------------
 */
static void
_freeFunc(Func *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeObject(node->func_tlist);
	freeObject(node->func_planlist);
	if (node->func_fcache)
		_freeFcache(node->func_fcache);

	pfree(node);
}

/* ----------------
 *		_freeAggref
 * ----------------
 */
static void
_freeAggref(Aggref *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	pfree(node->aggname);
	freeObject(node->target);

	pfree(node);
}

/* ----------------
 *		_freeSubLink
 * ----------------
 */
static void
_freeSubLink(SubLink *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeObject(node->lefthand);
	freeObject(node->oper);
	freeObject(node->subselect);

	pfree(node);
}

/* ----------------
 *		_freeRelabelType
 * ----------------
 */
static void
_freeRelabelType(RelabelType *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeObject(node->arg);

	pfree(node);
}

/* ----------------
 *		_freeCaseExpr
 * ----------------
 */
static void
_freeCaseExpr(CaseExpr *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeObject(node->arg);
	freeObject(node->args);
	freeObject(node->defresult);

	pfree(node);
}

/* ----------------
 *		_freeCaseWhen
 * ----------------
 */
static void
_freeCaseWhen(CaseWhen *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeObject(node->expr);
	freeObject(node->result);

	pfree(node);
}

static void
_freeArray(Array *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	pfree(node);
}

static void
_freeArrayRef(ArrayRef *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeObject(node->refupperindexpr);
	freeObject(node->reflowerindexpr);
	freeObject(node->refexpr);
	freeObject(node->refassgnexpr);

	pfree(node);
}

/* ****************************************************************
 *						relation.h free functions
 * ****************************************************************
 */

/* ----------------
 *		_freeRelOptInfo
 * ----------------
 */
static void
_freeRelOptInfo(RelOptInfo *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeList(node->relids);

	freeObject(node->targetlist);
	freeObject(node->pathlist);

	/*
	 * XXX is this right? cheapest-path fields will typically be pointers
	 * into pathlist, not separate structs...
	 */
	freeObject(node->cheapest_startup_path);
	freeObject(node->cheapest_total_path);

	freeObject(node->baserestrictinfo);
	freeObject(node->joininfo);
	freeObject(node->innerjoin);

	pfree(node);
}

/* ----------------
 *		_freeIndexOptInfo
 * ----------------
 */
static void
_freeIndexOptInfo(IndexOptInfo *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	if (node->classlist)
		pfree(node->classlist);

	if (node->indexkeys)
		pfree(node->indexkeys);

	if (node->ordering)
		pfree(node->ordering);

	freeObject(node->indpred);

	pfree(node);
}

/* ----------------
 *		FreePathFields
 *
 *		This function frees the fields of the Path node.  It is used by
 *		all the free functions for classes which inherit node Path.
 * ----------------
 */
static void
FreePathFields(Path *node)
{
	/* we do NOT free the parent; it doesn't belong to the Path */

	freeObject(node->pathkeys);
}

/* ----------------
 *		_freePath
 * ----------------
 */
static void
_freePath(Path *node)
{
	FreePathFields(node);

	pfree(node);
}

/* ----------------
 *		_freeIndexPath
 * ----------------
 */
static void
_freeIndexPath(IndexPath *node)
{
	/* ----------------
	 *	free the node superclass fields
	 * ----------------
	 */
	FreePathFields((Path *) node);

	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeList(node->indexid);
	freeObject(node->indexqual);
	freeList(node->joinrelids);

	pfree(node);
}

/* ----------------
 *		_freeTidPath
 * ----------------
 */
static void
_freeTidPath(TidPath *node)
{
	/* ----------------
	 *	free the node superclass fields
	 * ----------------
	 */
	FreePathFields((Path *) node);

	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeObject(node->tideval);
	freeList(node->unjoined_relids);

	pfree(node);
}

/* ----------------
 *		FreeJoinPathFields
 *
 *		This function frees the fields of the JoinPath node.  It is used by
 *		all the free functions for classes which inherit node JoinPath.
 * ----------------
 */
static void
FreeJoinPathFields(JoinPath *node)
{
	freeObject(node->outerjoinpath);
	freeObject(node->innerjoinpath);

	/*
	 * XXX probably wrong, since ordinarily a JoinPath would share its
	 * restrictinfo list with other paths made for the same join?
	 */
	freeObject(node->joinrestrictinfo);
}

/* ----------------
 *		_freeNestPath
 * ----------------
 */
static void
_freeNestPath(NestPath *node)
{
	/* ----------------
	 *	free the node superclass fields
	 * ----------------
	 */
	FreePathFields((Path *) node);
	FreeJoinPathFields((JoinPath *) node);

	pfree(node);
}

/* ----------------
 *		_freeMergePath
 * ----------------
 */
static void
_freeMergePath(MergePath *node)
{
	/* ----------------
	 *	free the node superclass fields
	 * ----------------
	 */
	FreePathFields((Path *) node);
	FreeJoinPathFields((JoinPath *) node);

	/* ----------------
	 *	free the remainder of the node
	 * ----------------
	 */
	freeObject(node->path_mergeclauses);
	freeObject(node->outersortkeys);
	freeObject(node->innersortkeys);

	pfree(node);
}

/* ----------------
 *		_freeHashPath
 * ----------------
 */
static void
_freeHashPath(HashPath *node)
{
	/* ----------------
	 *	free the node superclass fields
	 * ----------------
	 */
	FreePathFields((Path *) node);
	FreeJoinPathFields((JoinPath *) node);

	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeObject(node->path_hashclauses);

	pfree(node);
}

/* ----------------
 *		_freePathKeyItem
 * ----------------
 */
static void
_freePathKeyItem(PathKeyItem *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeObject(node->key);

	pfree(node);
}


/* ----------------
 *		_freeRestrictInfo
 * ----------------
 */
static void
_freeRestrictInfo(RestrictInfo *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeObject(node->clause);

	/*
	 * this is certainly wrong?  IndexOptInfos don't belong to
	 * RestrictInfo...
	 */
	freeObject(node->subclauseindices);

	pfree(node);
}

/* ----------------
 *		_freeJoinInfo
 * ----------------
 */
static void
_freeJoinInfo(JoinInfo *node)
{
	/* ----------------
	 *	free remainder of node
	 * ----------------
	 */
	freeList(node->unjoined_relids);
	freeObject(node->jinfo_restrictinfo);

	pfree(node);
}

static void
_freeIter(Iter *node)
{
	freeObject(node->iterexpr);

	pfree(node);
}

static void
_freeStream(Stream *node)
{
	freeObject(node->downstream);

	pfree(node);
}

/*
 *	parsenodes.h routines have no free functions
 */

static void
_freeTargetEntry(TargetEntry *node)
{
	freeObject(node->resdom);
	freeObject(node->fjoin);
	freeObject(node->expr);

	pfree(node);
}

static void
_freeRangeTblEntry(RangeTblEntry *node)
{
	if (node->relname)
		pfree(node->relname);
	freeObject(node->ref);
	freeObject(node->eref);

	pfree(node);
}

static void
_freeAttr(Attr *node)
{
	if (node->relname)
		pfree(node->relname);
	freeObject(node->attrs);

	pfree(node);
}

static void
_freeRowMark(RowMark *node)
{
	pfree(node);
}

static void
_freeSortClause(SortClause *node)
{
	pfree(node);
}

static void
_freeAConst(A_Const *node)
{
	freeObject(&(node->val));
	freeObject(node->typename);

	pfree(node);
}

static void
_freeTypeName(TypeName *node)
{
	if (node->name)
		pfree(node->name);
	freeObject(node->arrayBounds);

	pfree(node);
}

static void
_freeTypeCast(TypeCast *node)
{
	freeObject(node->arg);
	freeObject(node->typename);

	pfree(node);
}

static void
_freeQuery(Query *node)
{
	if (node->utilityStmt && nodeTag(node->utilityStmt) == T_NotifyStmt)
	{
		NotifyStmt *node_notify = (NotifyStmt *) node->utilityStmt;

		pfree(node_notify->relname);
		pfree(node_notify);
	}
	if (node->into)
		pfree(node->into);
	freeObject(node->rtable);
	freeObject(node->targetList);
	freeObject(node->qual);
	freeObject(node->rowMark);
	freeObject(node->distinctClause);
	freeObject(node->sortClause);
	freeObject(node->groupClause);
	freeObject(node->havingQual);
	/* why not intersectClause? */
	freeObject(node->unionClause);
	freeObject(node->limitOffset);
	freeObject(node->limitCount);

	/* XXX should we be freeing the planner internal fields? */

	pfree(node);
}


/*
 *	mnodes.h routines have no free functions
 */

/* ****************************************************************
 *					pg_list.h free functions
 * ****************************************************************
 */

static void
_freeValue(Value *node)
{
	switch (node->type)
	{
			case T_Float:
			case T_String:
			pfree(node->val.str);
			break;
		default:
			break;
	}

	pfree(node);
}

/* ----------------
 *		freeObject free's the node or list. If it is a list, it
 *		recursively frees its items.
 * ----------------
 */
static void
freeObject(void *node)
{
	if (node == NULL)
		return;

	switch (nodeTag(node))
	{

			/*
			 * PLAN NODES
			 */
		case T_Plan:
			_freePlan(node);
			break;
		case T_Result:
			_freeResult(node);
			break;
		case T_Append:
			_freeAppend(node);
			break;
		case T_Scan:
			_freeScan(node);
			break;
		case T_SeqScan:
			_freeSeqScan(node);
			break;
		case T_DelegatedSeqScan:
			_freeDelegatedSeqScan(node);
			break;
		case T_IndexScan:
			_freeIndexScan(node);
			break;
		case T_DelegatedIndexScan:
			_freeDelegatedIndexScan(node);
			break;
		case T_TidScan:
			_freeTidScan(node);
			break;
		case T_Join:
			_freeJoin(node);
			break;
		case T_NestLoop:
			_freeNestLoop(node);
			break;
		case T_MergeJoin:
			_freeMergeJoin(node);
			break;
		case T_HashJoin:
			_freeHashJoin(node);
			break;
		case T_Noname:
			_freeNoname(node);
			break;
		case T_Material:
			_freeMaterial(node);
			break;
		case T_Sort:
			_freeSort(node);
			break;
		case T_Group:
			_freeGroup(node);
			break;
		case T_Agg:
			_freeAgg(node);
			break;
		case T_GroupClause:
			_freeGroupClause(node);
			break;
		case T_Unique:
			_freeUnique(node);
			break;
		case T_Hash:
			_freeHash(node);
			break;
		case T_SubPlan:
			_freeSubPlan(node);
			break;

			/*
			 * PRIMITIVE NODES
			 */
		case T_Resdom:
			_freeResdom(node);
			break;
		case T_Fjoin:
			_freeFjoin(node);
			break;
		case T_Expr:
			_freeExpr(node);
			break;
		case T_Var:
			_freeVar(node);
			break;
		case T_Oper:
			_freeOper(node);
			break;
		case T_Const:
			_freeConst(node);
			break;
		case T_Param:
			_freeParam(node);
			break;
		case T_Func:
			_freeFunc(node);
			break;
		case T_Array:
			_freeArray(node);
			break;
		case T_ArrayRef:
			_freeArrayRef(node);
			break;
		case T_Aggref:
			_freeAggref(node);
			break;
		case T_SubLink:
			_freeSubLink(node);
			break;
		case T_RelabelType:
			_freeRelabelType(node);
			break;
		case T_CaseExpr:
			_freeCaseExpr(node);
			break;
		case T_CaseWhen:
			_freeCaseWhen(node);
			break;

			/*
			 * RELATION NODES
			 */
		case T_RelOptInfo:
			_freeRelOptInfo(node);
			break;
		case T_Path:
			_freePath(node);
			break;
		case T_IndexPath:
			_freeIndexPath(node);
			break;
		case T_TidPath:
			_freeTidPath(node);
			break;
		case T_NestPath:
			_freeNestPath(node);
			break;
		case T_MergePath:
			_freeMergePath(node);
			break;
		case T_HashPath:
			_freeHashPath(node);
			break;
		case T_PathKeyItem:
			_freePathKeyItem(node);
			break;
		case T_RestrictInfo:
			_freeRestrictInfo(node);
			break;
		case T_JoinInfo:
			_freeJoinInfo(node);
			break;
		case T_Iter:
			_freeIter(node);
			break;
		case T_Stream:
			_freeStream(node);
			break;
		case T_IndexOptInfo:
			_freeIndexOptInfo(node);
			break;

			/*
			 * PARSE NODES
			 */
		case T_Query:
			_freeQuery(node);
			break;
		case T_TargetEntry:
			_freeTargetEntry(node);
			break;
		case T_RangeTblEntry:
			_freeRangeTblEntry(node);
			break;
		case T_RowMark:
			_freeRowMark(node);
			break;
		case T_SortClause:
			_freeSortClause(node);
			break;
		case T_A_Const:
			_freeAConst(node);
			break;
		case T_TypeName:
			_freeTypeName(node);
			break;
		case T_TypeCast:
			_freeTypeCast(node);
			break;
		case T_Attr:
			_freeAttr(node);
			break;

			/*
			 * VALUE NODES
			 */
		case T_Integer:
		case T_Float:
		case T_String:
			_freeValue(node);
			break;
		case T_List:
			{
				List	   *list = node,
						   *l;

				foreach(l, list)
					freeObject(lfirst(l));
				freeList(list);
			}
			break;
		default:
			elog(ERROR, "freeObject: don't know how to free %d", nodeTag(node));
			break;
	}
}
