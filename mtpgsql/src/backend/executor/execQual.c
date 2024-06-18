/*-------------------------------------------------------------------------
 *
 * execQual.c
 *	  Routines to evaluate qualification and targetlist expressions
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/executor/execQual.c,v 1.1.1.1 2006/08/12 00:20:29 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
/*
 * INTERFACE ROUTINES ExecEvalExpr	- evaluate an expression and return a
 * datum ExecQual		- return true/false if qualification is
 * satisfied ExecTargetList	- form a new tuple by projecting the given
 * tuple
 * 
 * NOTES ExecEvalExpr() and ExecEvalVar() are hotspots.	making these faster
 * will speed up the entire system.  Unfortunately they are currently
 * implemented recursively.  Eliminating the recursion is bound to improve
 * the speed of the executor.
 * 
 * ExecTargetList() is used to make tuple projections.  Rather then trying to
 * speed it up, the execution plan should be pre-processed to facilitate
 * attribute sharing between nodes wherever possible, instead of doing
 * needless copying.	-cim 5/31/91
 * 
 */

#include "postgres.h"
#include "env/env.h"
#include "access/heapam.h"
#include "access/blobstorage.h"
#include "catalog/pg_language.h"
#include "executor/execFlatten.h"
#include "executor/execdebug.h"
#include "executor/executor.h"
#include "executor/functions.h"
#include "executor/nodeSubplan.h"
#include "utils/builtins.h"
#include "utils/fcache2.h"
#include "utils/java.h"
#include "utils/relcache.h"
#include "catalog/pg_type.h"

/*
 * externs and constants
 */

/*
 * XXX Used so we can get rid of use of Const nodes in the executor.
 * Currently only used by ExecHashGetBucket and set only by ExecMakeVarConst
 * and by ExecEvalArrayRef.
 */
/*
 * const bool		execConstByVal; const int
 * nstLen;
 */

/* static functions decls */
static Datum    ExecEvalAggref(Aggref * aggref, ExprContext * econtext, bool * isNull);
#ifndef NOARRAY
static Datum    ExecEvalArrayRef(ArrayRef * arrayRef, ExprContext * econtext,
		 bool * isNull, bool * isDone);
#endif
static Datum    ExecEvalAnd(Expr * andExpr, ExprContext * econtext, bool * isNull);
static Datum    ExecEvalFunc(Expr * funcClause, ExprContext * econtext, Oid * returnType,
                bool * isNull, bool * isDone);
static void 
ExecEvalFuncArgs(FunctionCachePtr fcache, ExprContext * econtext,
		 List * argList, Datum argV[], bool * argIsDone);
static void
ExecEvalJavaArgs(ExprContext * econtext, List * argList, Oid* types, Datum* argV);

static Datum    ExecEvalNot(Expr * notclause, ExprContext * econtext, bool * isNull);
static Datum    ExecEvalOper(Expr * opClause, ExprContext * econtext,
                                             bool * isNull);
static Datum    ExecEvalOr(Expr * orExpr, ExprContext * econtext, bool * isNull);

static Datum    
ExecMakeFunctionResult(Node * node, List * arguments, ExprContext * econtext, 
                bool * isNull, bool * isDone);
static Datum    ExecMakeJavaFunctionResult(Java * node, Datum target, Oid * dataType, 
                List * args, ExprContext * econtext, bool* isNull);

/*
 * ExecEvalArrayRef
 * 
 * This function takes an ArrayRef and returns a Const Node if it is an array
 * reference or returns the changed Array Node if it is an array assignment.
 */
#ifndef NOARRAY
	static          Datum
	                ExecEvalArrayRef(ArrayRef * arrayRef,
				                     ExprContext * econtext,
					                 bool * isNull,
					                 bool * isDone)
{
	ArrayType      *array_scanner;
	List           *elt;
	int             i = 0, j = 0;
	IntArray        upper, lower;
	int            *lIndex;
	bool            dummy;
	Env            *env = GetEnv();

	*isNull = false;

	if (arrayRef->refexpr != NULL) {
		array_scanner = (ArrayType *) ExecEvalExpr(arrayRef->refexpr,
							   econtext,
                                                           NULL,
							   isNull,
							   isDone);
		if (*isNull)
			return (Datum) NULL;
	} else {

		/*
		 * Null refexpr indicates we are doing an INSERT into an
		 * array column. For now, we just take the refassgnexpr
		 * (which the parser will have ensured is an array value) and
		 * return it as-is, ignoring any subscripts that may have
		 * been supplied in the INSERT column list. This is a kluge,
		 * but it's not real clear what the semantics ought to be...
		 */
		array_scanner = NULL;
	}

	foreach(elt, arrayRef->refupperindexpr) {
		if (i >= MAXDIM)
			elog(ERROR, "ExecEvalArrayRef: can only handle %d dimensions",
			     MAXDIM);

		upper.indx[i++] = (int32) ExecEvalExpr((Node *) lfirst(elt),
						       econtext,
                                                       NULL,
						       isNull,
						       &dummy);
		if (*isNull)
			return (Datum) NULL;
	}

	if (arrayRef->reflowerindexpr != NIL) {
		foreach(elt, arrayRef->reflowerindexpr) {
			if (j >= MAXDIM)
				elog(ERROR, "ExecEvalArrayRef: can only handle %d dimensions",
				     MAXDIM);

			lower.indx[j++] = (int32) ExecEvalExpr((Node *) lfirst(elt),
							       econtext,
                                                               NULL,
							       isNull,
							       &dummy);
			if (*isNull)
				return (Datum) NULL;
		}
		if (i != j)
			elog(ERROR,
			     "ExecEvalArrayRef: upper and lower indices mismatch");
		lIndex = lower.indx;
	} else
		lIndex = NULL;

	if (arrayRef->refassgnexpr != NULL) {
		Datum           sourceData = ExecEvalExpr(arrayRef->refassgnexpr,
							  econtext,
                                                          NULL,
							  isNull,
							  &dummy);

		if (*isNull)
			return (Datum) NULL;

		env->execConstByVal = arrayRef->refelembyval;
		env->execConstLen = arrayRef->refelemlength;

		if (array_scanner == NULL)
			return sourceData;	/* XXX do something else? */

		if (lIndex == NULL)
			return (Datum) array_set(array_scanner, i, upper.indx,
						 (char *) sourceData,
						 arrayRef->refelembyval,
						 arrayRef->refelemlength,
					   arrayRef->refattrlength, isNull);
		return (Datum) array_assgn(array_scanner, i, upper.indx,
					   lower.indx,
					   (ArrayType *) sourceData,
					   arrayRef->refelembyval,
					   arrayRef->refelemlength, isNull);
	}
	env->execConstByVal = arrayRef->refelembyval;
	env->execConstLen = arrayRef->refelemlength;

	if (lIndex == NULL)
		return (Datum) array_ref(array_scanner, i, upper.indx,
					 arrayRef->refelembyval,
					 arrayRef->refelemlength,
					 arrayRef->refattrlength, isNull);
	return (Datum) array_clip(array_scanner, i, upper.indx, lower.indx,
				  arrayRef->refelembyval,
				  arrayRef->refelemlength, isNull);
}
#endif				/* NOARRAY  */

/*
 * ----------------------------------------------------------------
 * ExecEvalAggref
 * 
 * Returns a Datum whose value is the value of the precomputed aggregate found
 * in the given expression context.
 * ----------------------------------------------------------------
 */
static          Datum
ExecEvalAggref(Aggref * aggref, ExprContext * econtext, bool * isNull)
{
	if (econtext->ecxt_aggvalues == NULL)	/* safety check */
		elog(ERROR, "ExecEvalAggref: no aggregates in this expression context");

	*isNull = econtext->ecxt_aggnulls[aggref->aggno];
	return econtext->ecxt_aggvalues[aggref->aggno];
}

/*
 * ----------------------------------------------------------------
 * ExecEvalVar
 * 
 * Returns a Datum whose value is the value of a range variable with respect to
 * given expression context.
 * 
 * 
 * As an entry condition, we expect that the datatype the plan expects to get
 * (as told by our "variable" argument) is in fact the datatype of the
 * attribute the plan says to fetch (as seen in the current context,
 * identified by our "econtext" argument).
 * 
 * If we fetch a Type A attribute and Caller treats it as if it were Type B,
 * there will be undefined results (e.g. crash). One way these might mismatch
 * now is that we're accessing a catalog class and the type information in
 * the pg_attribute class does not match the hardcoded pg_attribute
 * information (in pg_attribute.h) for the class in question.
 * 
 * We have an Assert to make sure this entry condition is met.
 * 
 * ----------------------------------------------------------------
 */
Datum
ExecEvalVar(Var * variable, ExprContext * econtext, bool * isNull, bool * byval, int * length)
{
	Datum           result;
	TupleTableSlot *slot;
	AttrNumber      attnum;
	HeapTuple       heapTuple;
	TupleDesc       tuple_type;


	/*
	 * get the slot we want
	 */
	switch (variable->varno) {
	case INNER:		/* get the tuple from the inner node */
		slot = econtext->ecxt_innertuple;
		break;

	case OUTER:		/* get the tuple from the outer node */
		slot = econtext->ecxt_outertuple;
		break;

	default:		/* get the tuple from the relation being
				 * scanned */
		slot = econtext->ecxt_scantuple;
		break;
	}

	/*
	 * extract tuple information from the slot
	 */
	heapTuple = slot->val;
	tuple_type = slot->ttc_tupleDescriptor;

	attnum = variable->varattno;

	/* (See prolog for explanation of this Assert) */
	Assert(attnum <= 0 ||
	       (attnum - 1 <= tuple_type->natts - 1 &&
		tuple_type->attrs[attnum - 1] != NULL )/*&&
	     variable->vartype == tuple_type->attrs[attnum - 1]->atttypid)
		 
		 experimenting with using streaming blobs so this check no longer valid
		 */
		 );

	/*
	 * If the attribute number is invalid, then we are supposed to return
	 * the entire tuple, we give back a whole slot so that callers know
	 * what the tuple looks like.
	 */
	if (attnum == InvalidAttrNumber) {
		TupleTableSlot *tempSlot;
		TupleDesc       td;
		HeapTuple       tup;

		tempSlot = ExecCreateTableSlot();

		ExecSetSlotDescriptor(tempSlot, tuple_type);
		ExecStoreTuple(heapTuple, tempSlot, false);
		return (Datum) tempSlot;
	}
	result = HeapGetAttr(heapTuple,	/* tuple containing attribute */
			     attnum,	/* attribute number of desired
					 * attribute */
			     tuple_type,	/* tuple descriptor of tuple */
			     isNull);	/* return: is attribute null? */
	/*
	 * return null if att is null
	 */
	if (*isNull)
		return (Datum) NULL;

	/*
	 * get length and type information.. ??? what should we do about
	 * variable length attributes - variable length attributes have their
	 * length stored in the first 4 bytes of the memory pointed to by the
	 * returned value.. If we can determine that the type is a variable
	 * length type, we can do the right thing. -cim 9/15/89
	 */
	if (attnum < 0) {

		/*
		 * If this is a pseudo-att, we get the type and fake the
		 * length. There ought to be a routine to return the real
		 * lengths, so we'll mark this one ... XXX -mao
		 */
		if ( length != NULL ) *length = heap_sysattrlen(attnum);	/* XXX see -mao above */
		if ( byval != NULL ) *byval = heap_sysattrbyval(attnum);	/* XXX see -mao above */
	} else {
		if ( length != NULL ) *length = tuple_type->attrs[attnum - 1]->attlen;
		if ( byval != NULL ) *byval = tuple_type->attrs[attnum - 1]->attbyval ? true : false;
                /*
		if ( tuple_type->attrs[attnum - 1]->attstorage == 'e' && !(*isNull) && ISINDIRECT(result) ) {
                    Relation        rel = econtext->ecxt_relation;
                    result = fix_header_blob(rel,result);
		}
                 */
	}

	return result;
}

/*
 * ----------------------------------------------------------------
 * ExecEvalParam
 * 
 * Returns the value of a parameter.  A param node contains something like
 * ($.name) and the expression context contains the current parameter
 * bindings (name = "sam") (age = 34)... so our job is to replace the param
 * node with the datum containing the appropriate information ("sam").
 * 
 * Q: if we have a parameter ($.foo) without a binding, i.e. there is no (foo =
 * xxx) in the parameter list info, is this a fatal error or should this be a
 * "not available" (in which case we shoud return a Const node with the
 * isnull flag) ?	-cim 10/13/89
 * 
 * Minor modification: Param nodes now have an extra field, `paramkind' which
 * specifies the type of parameter (see params.h). So while searching the
 * paramList for a paramname/value pair, we have also to check for `kind'.
 * 
 * NOTE: The last entry in `paramList' is always an entry with kind ==
 * PARAM_INVALID.
 * ----------------------------------------------------------------
 */
Datum
ExecEvalParam(Param * expression, ExprContext * econtext, bool * isNull)
{

	char           *thisParameterName;
	int             thisParameterKind = expression->paramkind;
	AttrNumber      thisParameterId = expression->paramid;
	int             matchFound;
	ParamListInfo   paramList;

	if (thisParameterKind == PARAM_EXEC) {
		ParamExecData  *prm = &(econtext->ecxt_param_exec_vals[thisParameterId]);

		if (prm->execPlan != NULL)
			ExecSetParamPlan(prm->execPlan);
		Assert(prm->execPlan == NULL);
		*isNull = prm->isnull;
		return prm->value;
	}
	thisParameterName = expression->paramname;
	paramList = econtext->ecxt_param_list_info;
	*isNull = false;
	/*
	 * search the list with the parameter info to find a matching name.
	 * An entry with an InvalidName denotes the last element in the
	 * array.
	 */
	matchFound = 0;
	if (paramList != NULL) {

		/*
		 * search for an entry in 'paramList' that matches the
		 * `expression'.
		 */
		while (paramList->kind != PARAM_INVALID && !matchFound) {
			switch (thisParameterKind) {
			case PARAM_NAMED:
				if (thisParameterKind == paramList->kind &&
				    strcmp(paramList->name, thisParameterName) == 0)
					matchFound = 1;
				break;
			case PARAM_NUM:
				if (thisParameterKind == paramList->kind &&
				    paramList->id == thisParameterId)
					matchFound = 1;
				break;
			case PARAM_OLD:
			case PARAM_NEW:
				if (thisParameterKind == paramList->kind &&
				    paramList->id == thisParameterId) {
					matchFound = 1;

					/*
					 * sanity check
					 */
					if (strcmp(paramList->name, thisParameterName) != 0) {
						elog(ERROR,
						     "ExecEvalParam: new/old params with same id & diff names");
					}
				}
				break;
			default:

				/*
				 * oops! this is not supposed to happen!
				 */
				elog(ERROR, "ExecEvalParam: invalid paramkind %d",
				     thisParameterKind);
			}
			if (!matchFound)
				paramList++;
		}		/* while */
	}			/* if */
	if (!matchFound) {

		/*
		 * ooops! we couldn't find this parameter in the parameter
		 * list. Signal an error
		 */
		elog(ERROR, "ExecEvalParam: Unknown value for parameter %s",
		     thisParameterName);
	}
	/*
	 * return the value.
	 */
	if (paramList->isnull) {
		*isNull = true;
		return (Datum) NULL;
	}
	if (expression->param_tlist != NIL) {
		HeapTuple       tup;
		Datum           value;
		List           *tlist = expression->param_tlist;
		TargetEntry    *tle = (TargetEntry *) lfirst(tlist);
		TupleTableSlot *slot = (TupleTableSlot *) paramList->value;

		tup = slot->val;
		value = ProjectAttribute(slot->ttc_tupleDescriptor,
					 tle, tup, isNull);
		return value;
	}
	return paramList->value;
}


/*
 * ----------------------------------------------------------------
 * ExecEvalOper / ExecEvalFunc support routines
 * ----------------------------------------------------------------
 */

static void
ExecEvalFuncArgs(FunctionCachePtr fcache,
		 ExprContext * econtext,
		 List * argList,
		 Datum argV[],
		 bool * argIsDone)
{
	int             i;
	bool           *nullVect;
	List           *arg;

	nullVect = fcache->nullVect;

	i = 0;
	foreach(arg, argList) {

		/*
		 * evaluate the expression, in general functions cannot take
		 * sets as arguments but we make an exception in the case of
		 * nested dot expressions.  We have to watch out for this
		 * case here.
		 */
		argV[i] = ExecEvalExpr((Node *) lfirst(arg),
				       econtext,
                                       NULL,
				       &nullVect[i],
				       argIsDone);

		if (!(*argIsDone)) {
			if (i != 0)
				elog(ERROR, "functions can only take sets in their first argument");
			fcache->setArg = (char *) argV[0];
			fcache->hasSetArg = true;
		}
		i++;
	}
}


static void
ExecEvalJavaArgs(ExprContext * econtext,
		 List * argList,
                 Oid*   argTypes,
		 Datum* argV)
{
	int             i;
	bool            nullVect;
	List           *arg;

	i = 0;
	foreach(arg, argList) {
		Node           *next = (Node *) lfirst(arg);
                bool isNull, isDone;

                argV[i] = ExecEvalExpr(next, econtext, &argTypes[i], &isNull, &isDone);

		i++;
	}
}

/*
 * ExecMakeJavaFunctionResult
 */
static          Datum
ExecMakeJavaFunctionResult(Java * node, Datum target, Oid * dataType, List * args, ExprContext * econtext, bool *isNull)
{
	Datum          jargV[FUNC_MAX_ARGS];
        Oid          jtypes[FUNC_MAX_ARGS];
        Oid          returnType;
        memset(jargV, '\0', sizeof(Datum) * FUNC_MAX_ARGS);
        memset(jtypes, '\0', sizeof(Oid) * FUNC_MAX_ARGS);
	/*
	 * arguments is a list of expressions to evaluate before passing to
	 * the function manager. We collect the results of evaluating the
	 * expressions into a datum array (argV) and pass this array to
	 * arrayFmgr()
	 */
	if (node->funcnargs != 0) {
		if (node->funcnargs > FUNC_MAX_ARGS)
			elog(ERROR, "ExecMakeJavaFunctionResult: too many arguments");

		ExecEvalJavaArgs(econtext, args, jtypes, jargV);
	}

	Datum result = fmgr_javaA(target, node->funcname,node->funcnargs,jtypes, jargV, &returnType, isNull);
        if (dataType != NULL) {
            *dataType = returnType;
        }
        return result;
}



/*
 * ExecMakeFunctionResult
 */
static          Datum
ExecMakeFunctionResult(Node * node,
		       List * arguments,
		       ExprContext * econtext,
		       bool * isNull,
		       bool * isDone)
{
	Datum           argV[FUNC_MAX_ARGS];
	FunctionCachePtr fcache;
	Func           *funcNode = NULL;
	Oper           *operNode = NULL;
	bool            funcisset = false;

	/*
	 * This is kind of ugly, Func nodes now have targetlists so that we
	 * know when and what to project out from postquel function results.
	 * This means we have to pass the func node all the way down instead
	 * of using only the fcache struct as before.  ExecMakeFunctionResult
	 * becomes a little bit more of a dual personality as a result.
	 */
	if (IsA(node, Func)) {
		funcNode = (Func *) node;
		fcache = funcNode->func_fcache;
	} else if (IsA(node, Oper)) {
		operNode = (Oper *) node;
		fcache = operNode->op_fcache;
	} else {
		elog(ERROR, "ExecMakeFunctionResult: unknown operation");
		return PointerGetDatum(NULL);
	}
	/*
	 * arguments is a list of expressions to evaluate before passing to
	 * the function manager. We collect the results of evaluating the
	 * expressions into a datum array (argV) and pass this array to
	 * arrayFmgr()
	 */
	if (fcache->nargs != 0) {
		bool            argDone;

		if (fcache->nargs > FUNC_MAX_ARGS)
			elog(ERROR, "ExecMakeFunctionResult: too many arguments");

		/*
		 * If the setArg in the fcache is set we have an argument
		 * returning a set of tuples (i.e. a nested dot expression).
		 * We don't want to evaluate the arguments again until the
		 * function is done. hasSetArg will always be false until we
		 * eval the args for the first time. We should set this in
		 * the parser.
		 */
		if ((fcache->hasSetArg) && fcache->setArg != NULL) {
			argV[0] = (Datum) fcache->setArg;
			argDone = false;
		} else {
			ExecEvalFuncArgs(fcache, econtext, arguments, argV, &argDone);
		}

		if ((fcache->hasSetArg) && (argDone)) {
			if (isDone)
				*isDone = true;
			return (Datum) NULL;
		}
	}
	/*
	 * If this function is really a set, we have to diddle with things.
	 * If the function has already been called at least once, then the
	 * setArg field of the fcache holds the OID of this set in pg_proc.
	 * (This is not quite legit, since the setArg field is really for
	 * functions which take sets of tuples as input - set functions take
	 * no inputs at all.	But it's a nice place to stash this value,
	 * for now.)
	 * 
	 * If this is the first call of the set's function, then the call to
	 * ExecEvalFuncArgs above just returned the OID of the pg_proc tuple
	 * which defines this set.	So replace the existing funcid in the
	 * funcnode with the set's OID.  Also, we want a new fcache which
	 * points to the right function, so get that, now that we have the
	 * right OID.  Also zero out the argV, since the real set doesn't
	 * take any arguments.
	 */
	if (((Func *) node)->funcid == F_SETEVAL) {
		funcisset = true;
		if (fcache->setArg) {
			argV[0] = 0;

			((Func *) node)->funcid = (Oid) PointerGetDatum(fcache->setArg);

		} else {
			((Func *) node)->funcid = (Oid) argV[0];
			setFcache(node, argV[0], NIL, econtext);
			fcache = ((Func *) node)->func_fcache;
			fcache->setArg = (char *) argV[0];
			argV[0] = (Datum) 0;
		}
	}
	/*
	 * now return the value gotten by calling the function manager,
	 * passing the function the evaluated parameter values.
	 */
	if (fcache->language == SQLlanguageId) {
		Datum           result;
		bool            argDone;

		Assert(funcNode);

		/*--------------------
		 * This loop handles the situation where we are iterating through
		 * all results in a nested dot function (whose argument function
		 * returns a set of tuples) and the current function finally
		 * finishes.  We need to get the next argument in the set and start
		 * the function all over again.  We might have to do it more than
		 * once, if the function produces no results for a particular argument.
		 * This is getting unclean.
		 *--------------------
		 */
		for (;;) {
			result = postquel_function(funcNode, (char **) argV,
						   isNull, isDone);

			if (!*isDone)
				break;	/* got a result from current argument */
			if (!fcache->hasSetArg)
				break;	/* input not a set, so done */

			/* OK, get the next argument... */
			ExecEvalFuncArgs(fcache, econtext, arguments, argV, &argDone);

			if (argDone) {

				/*
				 * End of arguments, so reset the setArg flag
				 * and say "Done"
				 */
				fcache->setArg = (char *) NULL;
				fcache->hasSetArg = false;
				*isDone = true;
				result = (Datum) NULL;
				break;
			}
			/*
			 * If we reach here, loop around to run the function
			 * on the new argument.
			 */
		}

		if (funcisset) {

			/*
			 * reset the funcid so that next call to this routine
			 * will still recognize this func as a set. Note that
			 * for now we assume that the set function in pg_proc
			 * must be a Postquel function - the funcid is not
			 * reset below for C functions.
			 */
			((Func *) node)->funcid = F_SETEVAL;

			/*
			 * If we're done with the results of this function,
			 * get rid of its func cache.
			 */
			if (*isDone)
				((Func *) node)->func_fcache = NULL;
		}
		return result;
	} else if ( fcache->language == JAVAlanguageId ) {
		int             i;
                Oid   returnType;
                Datum          args[FUNC_MAX_ARGS];
                Oid            types[FUNC_MAX_ARGS];
                JavaFunction       info = fcache->func.fn_data;

		if (isDone)
			*isDone = true;
		for (i = 0; i < fcache->nargs; i++)
			if (fcache->nullVect[i] == true)
				*isNull = true;

                ExecEvalJavaArgs(econtext,arguments,types,args);
		return fmgr_cached_javaA(info,fcache->nargs, args, &returnType, isNull);
        } else {
		int             i;

		if (isDone)
			*isDone = true;
		for (i = 0; i < fcache->nargs; i++)
			if (fcache->nullVect[i] == true)
				*isNull = true;

		return (Datum) fmgr_c(&fcache->func, (FmgrValues *) argV, isNull);
	}

}


/*
 * ----------------------------------------------------------------
 * ExecEvalOper ExecEvalFunc
 * 
 * Evaluate the functional result of a list of arguments by calling the function
 * manager.  Note that in the case of operator expressions, the optimizer had
 * better have already replaced the operator OID with the appropriate
 * function OID or we're hosed.
 * 
 * old comments Presumably the function manager will not take null arguments, so
 * we check for null arguments before sending the arguments to (fmgr).
 * 
 * Returns the value of the functional expression.
 * ----------------------------------------------------------------
 */

/*
 * ----------------------------------------------------------------
 * ExecEvalOper
 * ----------------------------------------------------------------
 */
static          Datum
ExecEvalOper(Expr * opClause, ExprContext * econtext, bool * isNull)
{
	Oper           *op;
	List           *argList;
	FunctionCachePtr fcache;
	bool            isDone;

	/*
	 * an opclause is a list (op args).  (I think)
	 * 
	 * we extract the oid of the function associated with the op and then
	 * pass the work onto ExecMakeFunctionResult which evaluates the
	 * arguments and returns the result of calling the function on the
	 * evaluated arguments.
	 */
	op = (Oper *) opClause->oper;
	argList = opClause->args;

	/*
	 * get the fcache from the Oper node. If it is NULL, then initialize
	 * it
	 */
	fcache = op->op_fcache;
	if (fcache == NULL) {
		setFcache((Node *) op, op->opid, argList, econtext);
		fcache = op->op_fcache;
	}
	/*
	 * call ExecMakeFunctionResult() with a dummy isDone that we ignore.
	 * We don't have operator whose arguments are sets.
	 */
	return ExecMakeFunctionResult((Node *) op, argList, econtext, isNull, &isDone);
}

/*
 * ----------------------------------------------------------------
 * ExecEvalFunc
 * ----------------------------------------------------------------
 */

static          Datum
ExecEvalFunc(Expr * funcClause,
	     ExprContext * econtext,
             Oid * returnType,
	     bool * isNull,
	     bool * isDone)
{
	Node           *fn;
	List           *argList;

	if (IsA(funcClause->oper, Func)) {
		Func           *func;

		FunctionCachePtr fcache;

		/*
		 * an funcclause is a list (func args).  (I think)
		 * 
		 * we extract the oid of the function associated with the func
		 * node and then pass the work onto ExecMakeFunctionResult
		 * which evaluates the arguments and returns the result of
		 * calling the function on the evaluated arguments.
		 * 
		 * this is nearly identical to the ExecEvalOper code.
		 */
		func = (Func *) funcClause->oper;
		argList = funcClause->args;

                if (returnType != NULL) {
                    *returnType = func->functype;
                }

		/*
		 * get the fcache from the Func node. If it is NULL, then
		 * initialize it
		 */
		fcache = func->func_fcache;
		if (fcache == NULL) {
			setFcache((Node *) func, func->funcid, argList, econtext);
			fcache = func->func_fcache;
		}
		fn = (Node *) func;
		return ExecMakeFunctionResult(fn, argList, econtext, isNull, isDone);

	} else {
		Java           *javaNode = (Java *) funcClause->oper;
		bool            done, isn;
		Datum           javaTarget = PointerGetDatum(NULL);
		if (javaNode->java_target)
			javaTarget = ExecEvalExpr(javaNode->java_target, econtext, NULL, &done, &isn);

		return ExecMakeJavaFunctionResult(javaNode, javaTarget, returnType, funcClause->args, econtext,isNull);
	}
}

/*
 * ----------------------------------------------------------------
 * ExecEvalNot ExecEvalOr ExecEvalAnd
 * 
 * Evaluate boolean expressions.  Evaluation of 'or' is short-circuited when the
 * first true (or null) value is found.
 * 
 * The query planner reformulates clause expressions in the qualification to
 * conjunctive normal form.  If we ever get an AND to evaluate, we can be
 * sure that it's not a top-level clause in the qualification, but appears
 * lower (as a function argument, for example), or in the target list.	Not
 * that you need to know this, mind you...
 * ----------------------------------------------------------------
 */
static          Datum
ExecEvalNot(Expr * notclause, ExprContext * econtext, bool * isNull)
{
	Node           *clause;
	Datum           expr_value;
	bool            isDone;

	clause = lfirst(notclause->args);

	/*
	 * We don't iterate over sets in the quals, so pass in an isDone
	 * flag, but ignore it.
	 */
	expr_value = ExecEvalExpr(clause, econtext, NULL, isNull, &isDone);

	/*
	 * if the expression evaluates to null, then we just cascade the null
	 * back to whoever called us.
	 */
	if (*isNull)
		return expr_value;

	/*
	 * evaluation of 'not' is simple.. expr is false, then return 'true'
	 * and vice versa.
	 */
	if (DatumGetChar(expr_value) == 0)
		return (Datum) true;

	return (Datum) false;
}

/*
 * ----------------------------------------------------------------
 * ExecEvalOr
 * ----------------------------------------------------------------
 */
static          Datum
ExecEvalOr(Expr * orExpr, ExprContext * econtext, bool * isNull)
{
	List           *clauses;
	List           *clause;
	bool            isDone;
	bool            AnyNull;
	Datum           clause_value;

	clauses = orExpr->args;
	AnyNull = false;

	/*
	 * If any of the clauses is TRUE, the OR result is TRUE regardless of
	 * the states of the rest of the clauses, so we can stop evaluating
	 * and return TRUE immediately.  If none are TRUE and one or more is
	 * NULL, we return NULL; otherwise we return FALSE.  This makes sense
	 * when you interpret NULL as "don't know": if we have a TRUE then
	 * the OR is TRUE even if we aren't sure about some of the other
	 * inputs. If all the known inputs are FALSE, but we have one or more
	 * "don't knows", then we have to report that we "don't know" what
	 * the OR's result should be --- perhaps one of the "don't knows"
	 * would have been TRUE if we'd known its value.  Only when all the
	 * inputs are known to be FALSE can we state confidently that the
	 * OR's result is FALSE.
	 */
	foreach(clause, clauses) {

		/*
		 * We don't iterate over sets in the quals, so pass in an
		 * isDone flag, but ignore it.
		 */
		clause_value = ExecEvalExpr((Node *) lfirst(clause),
					    econtext,
                                            NULL,
					    isNull,
					    &isDone);

		/*
		 * if we have a non-null true result, then return it.
		 */
		if (*isNull)
			AnyNull = true;	/* remember we got a null */
		else if (DatumGetChar(clause_value) != 0)
			return clause_value;
	}

	/* AnyNull is true if at least one clause evaluated to NULL */
	*isNull = AnyNull;
	return (Datum) false;
}

/*
 * ----------------------------------------------------------------
 * ExecEvalAnd
 * ----------------------------------------------------------------
 */
static          Datum
ExecEvalAnd(Expr * andExpr, ExprContext * econtext, bool * isNull)
{
	List           *clauses;
	List           *clause;
	bool            isDone;
	bool            AnyNull;
	Datum           clause_value;

	clauses = andExpr->args;
	AnyNull = false;

	/*
	 * If any of the clauses is FALSE, the AND result is FALSE regardless
	 * of the states of the rest of the clauses, so we can stop
	 * evaluating and return FALSE immediately.  If none are FALSE and
	 * one or more is NULL, we return NULL; otherwise we return TRUE.
	 * This makes sense when you interpret NULL as "don't know", using
	 * the same sort of reasoning as for OR, above.
	 */
	foreach(clause, clauses) {

		/*
		 * We don't iterate over sets in the quals, so pass in an
		 * isDone flag, but ignore it.
		 */
		clause_value = ExecEvalExpr((Node *) lfirst(clause),
					    econtext,
                                            NULL,
					    isNull,
					    &isDone);

		/*
		 * if we have a non-null false result, then return it.
		 */
		if (*isNull)
			AnyNull = true;	/* remember we got a null */
		else if (DatumGetChar(clause_value) == 0)
			return clause_value;
	}

	/* AnyNull is true if at least one clause evaluated to NULL */
	*isNull = AnyNull;
	return (Datum) (!AnyNull);
}

/*
 * ----------------------------------------------------------------
 * ExecEvalCase
 * 
 * Evaluate a CASE clause. Will have boolean expressions inside the WHEN
 * clauses, and will have expressions for results. - thomas 1998-11-09
 * ----------------------------------------------------------------
 */
static          Datum
ExecEvalCase(CaseExpr * caseExpr, ExprContext * econtext, bool * isNull)
{
	List           *clauses;
	List           *clause;
	Datum           clause_value;
	bool            isDone;

	clauses = caseExpr->args;

	/*
	 * we evaluate each of the WHEN clauses in turn, as soon as one is
	 * true we return the corresponding result. If none are true then we
	 * return the value of the default clause, or NULL if there is none.
	 */
	foreach(clause, clauses) {
		CaseWhen       *wclause = lfirst(clause);

		/*
		 * We don't iterate over sets in the quals, so pass in an
		 * isDone flag, but ignore it.
		 */
		clause_value = ExecEvalExpr(wclause->expr,
					    econtext,
                                            NULL,
					    isNull,
					    &isDone);

		/*
		 * if we have a true test, then we return the result, since
		 * the case statement is satisfied.  A NULL result from the
		 * test is not considered true.
		 */
		if (DatumGetChar(clause_value) != 0 && !*isNull) {
			return ExecEvalExpr(wclause->result,
					    econtext,
                                            NULL,
					    isNull,
					    &isDone);
		}
	}

	if (caseExpr->defresult) {
		return ExecEvalExpr(caseExpr->defresult,
				    econtext,
                                    NULL,
				    isNull,
				    &isDone);
	}
	*isNull = true;
	return (Datum) 0;
}

/*
 * ----------------------------------------------------------------
 * ExecEvalExpr
 * 
 * Recursively evaluate a targetlist or qualification expression.
 * 
 * This routine is an inner loop routine and should be as fast as possible.
 * 
 * Node comparison functions were replaced by macros for speed and to plug
 * memory leaks incurred by using the planner's Lispy stuff for comparisons.
 * Order of evaluation of node comparisons IS IMPORTANT; the macros do no
 * checks.  Order of evaluation:
 * 
 * o an isnull check, largely to avoid coredumps since greg doubts this routine
 * is called with a null ptr anyway in proper operation, but is not
 * completely sure... o ExactNodeType checks. o clause checks or other checks
 * where we look at the lfirst of something.
 * ----------------------------------------------------------------
 */
Datum
ExecEvalExpr(Node * expression,
	     ExprContext * econtext,
             Oid * dataType,
	     bool * isNull,
	     bool * isDone)
{
	Datum           retDatum;
        Oid             returnType = InvalidOid;

	*isNull = false;

	/*
	 * Some callers don't care about is done and only want 1 result.
	 * They indicate this by passing NULL
	 */
	if (isDone)
		*isDone = true;

	/*
	 * here we dispatch the work to the appropriate type of function
	 * given the type of our expression.
	 */
	if (expression == NULL) {
		*isNull = true;
		return (Datum) true;
	}
	switch (nodeTag(expression)) {
	case T_Var:
                returnType = ((Var *) expression)->vartype;
		retDatum = ExecEvalVar((Var *) expression, econtext, isNull, NULL,NULL);
		break;
	case T_Const:
		{
			Const          *con = (Const *) expression;
                        returnType = ((Const *) expression)->consttype;
			retDatum = con->constvalue;
			*isNull = con->constisnull;
			break;
		}
	case T_Param:
                returnType = ((Param *) expression)->paramtype;
		retDatum = ExecEvalParam((Param *) expression, econtext, isNull);
		break;
	case T_Iter:
                returnType = ((Iter *) expression)->itertype;
		retDatum = ExecEvalIter((Iter *) expression,
					econtext,
					isNull,
					isDone);
		break;
	case T_Aggref:
                returnType = ((Aggref *) expression)->aggtype;
		retDatum = ExecEvalAggref((Aggref *) expression, econtext, isNull);
		break;
#ifndef NOARRAY
	case T_ArrayRef:
                returnType = ((ArrayRef *) expression)->refelemtype;
		retDatum = ExecEvalArrayRef((ArrayRef *) expression,
					    econtext,
					    isNull,
					    isDone);
		break;
#endif
	case T_Expr:
		{
			Expr           *expr = (Expr *) expression;
                        returnType = expr->typeOid;
			switch (expr->opType) {
			case OP_EXPR:
				retDatum = ExecEvalOper(expr, econtext, isNull);
				break;
			case FUNC_EXPR:
				retDatum = ExecEvalFunc(expr, econtext, &returnType,
							isNull, isDone);
				break;
			case OR_EXPR:
				retDatum = ExecEvalOr(expr, econtext, isNull);
				break;
			case AND_EXPR:
				retDatum = ExecEvalAnd(expr, econtext, isNull);
				break;
			case NOT_EXPR:
				retDatum = ExecEvalNot(expr, econtext, isNull);
				break;
			case SUBPLAN_EXPR:
				retDatum = ExecSubPlan((SubPlan *) expr->oper,
						       expr->args, econtext,
						       isNull);
				break;
			default:
				elog(ERROR, "ExecEvalExpr: unknown expression type %d",
				     expr->opType);
				retDatum = 0;	/* keep compiler quiet */
				break;
			}
			break;
		}
	case T_RelabelType:
                {
                    Oid checkType;
                    returnType = ((RelabelType *) expression)->resulttype;
                    retDatum = ExecEvalExpr(((RelabelType *) expression)->arg,
                                            econtext,
                                            &checkType,
                                            isNull,
                                            isDone);
                    if (checkType != returnType) {
                        elog(NOTICE, "relabel return type does not equal expected type %ld != %ld", checkType, returnType);
                    }
                }
		break;
	case T_CaseExpr:
                returnType = ((CaseExpr *) expression)->casetype;
		retDatum = ExecEvalCase((CaseExpr *) expression, econtext, isNull);
		break;

	default:
		elog(ERROR, "ExecEvalExpr: unknown expression type %d",
		     nodeTag(expression));
		retDatum = 0;	/* keep compiler quiet */
		break;
	}
        if (dataType != NULL) {
           *dataType = returnType;
        }
	return retDatum;
}				/* ExecEvalExpr() */


/*
 * ---------------------------------------------------------------- ExecQual
 * / ExecTargetList
 * ----------------------------------------------------------------
 */

/*
 * ---------------------------------------------------------------- ExecQual
 * 
 * Evaluates a conjunctive boolean expression (qual list) and returns true iff
 * none of the subexpressions are false. (We also return true if the list is
 * empty.)
 * 
 * If some of the subexpressions yield NULL but none yield FALSE, then the
 * result of the conjunction is NULL (ie, unknown) according to three-valued
 * boolean logic.  In this case, we return the value specified by the
 * "resultForNull" parameter.
 * 
 * Callers evaluating WHERE clauses should pass resultForNull=FALSE, since SQL
 * specifies that tuples with null WHERE results do not get selected.  On the
 * other hand, callers evaluating constraint conditions should pass
 * resultForNull=TRUE, since SQL also specifies that NULL constraint
 * conditions are not failures.
 * 
 * NOTE: it would not be correct to use this routine to evaluate an AND
 * subclause of a boolean expression; for that purpose, a NULL result must be
 * returned as NULL so that it can be properly treated in the next higher
 * operator (cf. ExecEvalAnd and ExecEvalOr). This routine is only used in
 * contexts where a complete expression is being evaluated and we know that
 * NULL can be treated the same as one boolean result or the other.
 * 
 * ----------------------------------------------------------------
 */
bool
ExecQual(List * qual, ExprContext * econtext, bool resultForNull)
{
	List           *qlist;

	/*
	 * debugging stuff
	 */
	EV_printf("ExecQual: qual is ");
	EV_nodeDisplay(qual);
	EV_printf("\n");

	IncrProcessed();

	/*
	 * Evaluate the qual conditions one at a time.	If we find a FALSE
	 * result, we can stop evaluating and return FALSE --- the AND result
	 * must be FALSE.  Also, if we find a NULL result when resultForNull
	 * is FALSE, we can stop and return FALSE --- the AND result must be
	 * FALSE or NULL in that case, and the caller doesn't care which.
	 * 
	 * If we get to the end of the list, we can return TRUE.  This will
	 * happen when the AND result is indeed TRUE, or when the AND result
	 * is NULL (one or more NULL subresult, with all the rest TRUE) and
	 * the caller has specified resultForNull = TRUE.
	 */

	foreach(qlist, qual) {
		Node           *clause = (Node *) lfirst(qlist);
		Datum           expr_value;
		bool            isNull;
		bool            isDone;
                Oid             expr_type;

		/*
		 * If there is a null clause, consider the qualification to
		 * fail. XXX is this still correct for constraints?  It
		 * probably shouldn't happen at all ...
		 */
		if (clause == NULL)
			return false;

		/*
		 * pass isDone, but ignore it.	We don't iterate over
		 * multiple returns in the qualifications.
		 */
		expr_value = ExecEvalExpr(clause, econtext, &expr_type, &isNull, &isDone);

		if (isNull) {
			if (resultForNull == false)
				return false;	/* treat NULL as FALSE */
		} else {
			if (DatumGetChar(expr_value) == 0)
				return false;	/* definitely FALSE */
		}
	}

	return true;
}

int
ExecTargetListLength(List * targetlist)
{
	int             len;
	List           *tl;
	TargetEntry    *curTle;

	len = 0;
	foreach(tl, targetlist) {
		curTle = lfirst(tl);

		if (curTle->resdom != NULL)
			len++;
		else
			len += curTle->fjoin->fj_nNodes;
	}
	return len;
}

/*
 * ----------------------------------------------------------------
 * ExecTargetList
 * 
 * Evaluates a targetlist with respect to the current expression context and
 * return a tuple.
 * ----------------------------------------------------------------
 */
static          HeapTuple
ExecTargetList(List * targetlist,
	       int nodomains,
	       TupleDesc targettype,
	       Datum * values,
	       ExprContext * econtext,
	       bool * isDone)
{
	char            nulls_array[64];
	bool            fjNullArray[64];
	bool            itemIsDoneArray[64];
	char           *null_head;
	bool           *fjIsNull;
	bool           *itemIsDone;
	List           *tl;
	TargetEntry    *tle;
	Node           *expr;
	Resdom         *resdom;
	AttrNumber      resind;
	Datum           constvalue;
        Oid             consttype;
	HeapTuple       newTuple;
	bool            isNull;
	bool            haveDoneIters;
	static struct tupleDesc NullTupleDesc;	/* we assume this inits to
						 * zeroes */

	/*
	 * debugging stuff
	 */
	EV_printf("ExecTargetList: tl is ");
	EV_nodeDisplay(targetlist);
	EV_printf("\n");

	/*
	 * There used to be some klugy and demonstrably broken code here that
	 * special-cased the situation where targetlist == NIL.  Now we just
	 * fall through and return an empty-but-valid tuple.  We do, however,
	 * have to cope with the possibility that targettype is NULL ---
	 * heap_formtuple won't like that, so pass a dummy descriptor with
	 * natts = 0 to deal with it.
	 */
	if (targettype == NULL)
		targettype = &NullTupleDesc;

	/*
	 * allocate an array of char's to hold the "null" information only if
	 * we have a really large targetlist.  otherwise we use the stack.
	 * 
	 * We also allocate a bool array that is used to hold fjoin result
	 * state, and another that holds the isDone status for each
	 * targetlist item.
	 */
	if (nodomains > 64) {
		null_head = (char *) palloc(nodomains + 1);
		fjIsNull = (bool *) palloc(nodomains + 1);
		itemIsDone = (bool *) palloc(nodomains + 1);
	} else {
		null_head = &nulls_array[0];
		fjIsNull = &fjNullArray[0];
		itemIsDone = &itemIsDoneArray[0];
	}

	/*
	 * evaluate all the expressions in the target list
	 */

	*isDone = true;		/* until proven otherwise */
	haveDoneIters = false;	/* any isDone Iter exprs in tlist? */

	foreach(tl, targetlist) {

		/*
		 * remember, a target list is a list of lists:
		 * 
		 * ((<resdom | fjoin> expr) (<resdom | fjoin> expr) ...)
		 * 
		 * tl is a pointer to successive cdr's of the targetlist tle is
		 * a pointer to the target list entry in tl
		 */
		tle = lfirst(tl);

		if (tle->resdom != NULL) {
			expr = tle->expr;
			resdom = tle->resdom;
			resind = resdom->resno - 1;

			constvalue = (Datum) ExecEvalExpr(expr,
							  econtext,
                                                          &consttype,
							  &isNull,
						       &itemIsDone[resind]);
                        if (targettype->attrs[resind]->atttypid == UNKNOWNOID) {
                            targettype->attrs[resind]->atttypid = consttype;
                        }
			values[resind] = constvalue;

			if (!isNull)
				null_head[resind] = ' ';
			else
				null_head[resind] = 'n';

			if (IsA(expr, Iter)) {
				if (itemIsDone[resind])
					haveDoneIters = true;
				else
					*isDone = false;	/* we have undone Iters
								 * in the list */
			}
		} else {
			int             curNode;
			Resdom         *fjRes;
			List           *fjTlist = (List *) tle->expr;
			Fjoin          *fjNode = tle->fjoin;
			int             nNodes = fjNode->fj_nNodes;
			DatumPtr        results = fjNode->fj_results;

			ExecEvalFjoin(tle, econtext, fjIsNull, isDone);

			/* this is probably wrong: */
			if (*isDone)
				return (HeapTuple) NULL;

			/*
			 * get the result from the inner node
			 */
			fjRes = (Resdom *) fjNode->fj_innerNode;
			resind = fjRes->resno - 1;
			if (fjIsNull[0])
				null_head[resind] = 'n';
			else {
				null_head[resind] = ' ';
				values[resind] = results[0];
			}

			/*
			 * Get results from all of the outer nodes
			 */
			for (curNode = 1;
			     curNode < nNodes;
			     curNode++, fjTlist = lnext(fjTlist)) {
#ifdef NOT_USED			/* what is this?? */
				Node           *outernode = lfirst(fjTlist);

				fjRes = (Resdom *) outernode->iterexpr;
#endif
				resind = fjRes->resno - 1;
				if (fjIsNull[curNode])
					null_head[resind] = 'n';
				else {
					null_head[resind] = ' ';
					values[resind] = results[curNode];
				}
			}
		}
	}

	if (haveDoneIters) {
		if (*isDone) {

			/*
			 * all Iters are done, so return a null indicating
			 * tlist set expansion is complete.
			 */
			newTuple = NULL;
			goto exit;
		} else {

			/*
			 * We have some done and some undone Iters.  Restart
			 * the done ones so that we can deliver a tuple (if
			 * possible).
			 * 
			 * XXX this code is a crock, because it only works for
			 * Iters at the top level of tlist expressions, and
			 * doesn't even work right for them: you should get
			 * all possible combinations of Iter results, but you
			 * won't unless the numbers of values returned by
			 * each are relatively prime.  Should have a
			 * mechanism more like aggregate functions, where we
			 * make a list of all Iters contained in the tlist
			 * and cycle through their values in a methodical
			 * fashion.  To do someday; can't get excited about
			 * fixing a Berkeley feature that's not in SQL92.
			 * (The only reason we're doing this much is that we
			 * have to be sure all the Iters are run to
			 * completion, or their subplan executors will have
			 * unreleased resources, e.g. pinned buffers...)
			 */
			foreach(tl, targetlist) {
				tle = lfirst(tl);

				if (tle->resdom != NULL) {
					expr = tle->expr;
					resdom = tle->resdom;
					resind = resdom->resno - 1;

					if (IsA(expr, Iter) && itemIsDone[resind]) {
						constvalue = (Datum) ExecEvalExpr(expr,
								   econtext,
                                                                   &consttype,
								    &isNull,
						       &itemIsDone[resind]);
						if (itemIsDone[resind]) {

							/*
							 * Oh dear, this Iter
							 * is returning an
							 * empty set. Guess
							 * we can't make a
							 * tuple after all.
							 */
							*isDone = true;
							newTuple = NULL;
							goto exit;
						}
                                                if (targettype->attrs[resind]->atttypid == UNKNOWNOID) {
                                                    targettype->attrs[resind]->atttypid = consttype;
                                                }
						values[resind] = constvalue;

						if (!isNull)
							null_head[resind] = ' ';
						else
							null_head[resind] = 'n';
					}
				}
			}
		}
	}
	/*
	 * form the new result tuple (in the "normal" context)
	 */
	newTuple = (HeapTuple) heap_formtuple(targettype, values, null_head);
	if (econtext->ecxt_scantuple != NULL) {
		newTuple->t_self = econtext->ecxt_scantuple->val->t_self;
          /*  we may want to know the exact visiblity of this tuple 
           *  (ie HardCommit so copy this info as well  
           */
                newTuple->t_data->t_xmin = econtext->ecxt_scantuple->val->t_data->t_xmin;
                newTuple->t_data->t_xmax = econtext->ecxt_scantuple->val->t_data->t_xmax;
                newTuple->t_data->progress = econtext->ecxt_scantuple->val->t_data->progress;
	}
exit:

	/*
	 * free the status arrays if we palloc'd them
	 */
	if (nodomains > 64) {
		pfree(null_head);
		pfree(fjIsNull);
		pfree(itemIsDone);
	}
	return newTuple;
}

/*
 * ----------------------------------------------------------------
 * ExecProject
 * 
 * projects a tuple based in projection info and stores it in the specified
 * tuple table slot.
 * 
 * Note: someday soon the executor can be extended to eliminate redundant
 * projections by storing pointers to datums in the tuple table and then
 * passing these around when possible.  this should make things much quicker.
 * -cim 6/3/91
 * ----------------------------------------------------------------
 */
TupleTableSlot *
ExecProject(ProjectionInfo * projInfo, bool * isDone)
{
	TupleTableSlot *slot;
	List           *targetlist;
	int             len;
	TupleDesc       tupType;
	Datum          *tupValue;
	ExprContext    *econtext;
	HeapTuple       newTuple;

	/*
	 * sanity checks
	 */
	if (projInfo == NULL)
		return (TupleTableSlot *) NULL;

	/*
	 * get the projection info we want
	 */
	slot = projInfo->pi_slot;
	targetlist = projInfo->pi_targetlist;
	len = projInfo->pi_len;
	tupType = slot->ttc_tupleDescriptor;

	tupValue = projInfo->pi_tupValue;
	econtext = projInfo->pi_exprContext;

	/*
	 * form a new (result) tuple
	 */
	newTuple = ExecTargetList(targetlist,
				  len,
				  tupType,
				  tupValue,
				  econtext,
				  isDone);


	/*
	 * store the tuple in the projection slot and return the slot.
	 */
	return ExecStoreTuple(newTuple, slot, false);
}
