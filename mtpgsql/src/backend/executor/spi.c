/*-------------------------------------------------------------------------
 *
 * spi.c
 *				Server Programming Interface
 *
 * Portions Copyright (c) 1996-2001, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/executor/spi.c,v 1.1.1.1 2006/08/12 00:20:36 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "env/env.h"

#include "access/heapam.h"
#include "access/printtup.h"
#include "catalog/heap.h"
#include "commands/command.h"
#include "executor/spi_priv.h"
#include "tcop/tcopprot.h"
#include "utils/lsyscache.h"

#define DatumGetCString(X) ((char *) DatumGetPointer(X))


static TupleTableSlot *
MakeTupleTableSlot(void);
static Form_pg_attribute SystemAttributeByName(const char *attname,
					  bool relhasoids);
static Form_pg_attribute
SystemAttributeDefinition(AttrNumber attno, bool relhasoids);
static bool
getTypeOutputInfo(Oid type, Oid *typOutput, Oid *typElem,
				  bool *typIsVarlena);

typedef struct InternalSPIInfo {
	 _SPI_connection *_SPI_stack;
	_SPI_connection *_SPI_current;
	int	_SPI_connected;
	int	_SPI_curid;
    int unnamed_portal_count;
} InternalSPIInfo;

typedef struct SPIGlobal {
    SPIInfo    		   info;
    InternalSPIInfo    internal;
} SPIGlobal;

static SectionId  spi_id = SECTIONID("SPID");

static InternalSPIInfo* GetInternalSPIInfo(void);

static int	_SPI_execute(char *src, int tcount, _SPI_plan *plan);
static int	_SPI_pquery(QueryDesc *queryDesc, EState *state, int tcount);

static int _SPI_execute_plan(_SPI_plan *plan,
				  Datum *Values, char *Nulls, int tcount);

static void _SPI_cursor_operation(Portal portal, bool forward, int count,
					  CommandDest dest);

static _SPI_plan *_SPI_copy_plan(_SPI_plan *plan, int location);

static int	_SPI_begin_call(bool execmem);
static int	_SPI_end_call(bool procmem);
static MemoryContext _SPI_execmem(void);
static MemoryContext _SPI_procmem(void);
static bool _SPI_checktuples(void);


/* =================== interface functions =================== */

int
SPI_connect(void)
{
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	_SPI_connection *new_SPI_stack;

	/*
	 * When procedure called by Executor internal->_SPI_curid expected to be equal
	 * to _SPI_connected
	 */
	if (internal->_SPI_curid != internal->_SPI_connected)
		return SPI_ERROR_CONNECT;

	if (internal->_SPI_stack == NULL)
	{
		if (internal->_SPI_connected != -1)
			elog(FATAL, "SPI_connect: no connection(s) expected");
		new_SPI_stack = (_SPI_connection *) malloc(sizeof(_SPI_connection));
	}
	else
	{
		if (internal->_SPI_connected <= -1)
			elog(FATAL, "SPI_connect: some connection(s) expected");
		new_SPI_stack = (_SPI_connection *) realloc(internal->_SPI_stack,
						 (internal->_SPI_connected + 2) * sizeof(_SPI_connection));
	}

	if (new_SPI_stack == NULL)
		elog(ERROR, "Memory exhausted in SPI_connect");

	/*
	 * We' returning to procedure where internal->_SPI_curid == internal->_SPI_connected - 1
	 */
	internal->_SPI_stack = new_SPI_stack;
	internal->_SPI_connected++;

	internal->_SPI_current = &(internal->_SPI_stack[internal->_SPI_connected]);
	internal->_SPI_current->qtlist = NULL;
	internal->_SPI_current->processed = 0;
	internal->_SPI_current->tuptable = NULL;

	/* Create memory contexts for this procedure */
	internal->_SPI_current->procCxt = AllocSetContextCreate(MemoryContextGetEnv()->TopTransactionContext,
												  "SPI Proc",
												ALLOCSET_DEFAULT_MINSIZE,
											   ALLOCSET_DEFAULT_INITSIZE,
											   ALLOCSET_DEFAULT_MAXSIZE);
	internal->_SPI_current->execCxt = AllocSetContextCreate(MemoryContextGetEnv()->TopTransactionContext,
												  "SPI Exec",
												ALLOCSET_DEFAULT_MINSIZE,
											   ALLOCSET_DEFAULT_INITSIZE,
											   ALLOCSET_DEFAULT_MAXSIZE);
	/* ... and switch to procedure's context */
	internal->_SPI_current->savedcxt = MemoryContextSwitchTo(internal->_SPI_current->procCxt);

	internal->_SPI_current->savedId = GetScanCommandId();
	SetScanCommandId(GetCurrentCommandId());

	return SPI_OK_CONNECT;

}

int
SPI_finish(void)
{
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	int			res;

	res = _SPI_begin_call(false);		/* live in procedure memory */
	if (res < 0)
		return res;

	/* Restore memory context as it was before procedure call */
	MemoryContextSwitchTo(internal->_SPI_current->savedcxt);

	/* Release memory used in procedure call */
	MemoryContextDelete(internal->_SPI_current->execCxt);
	MemoryContextDelete(internal->_SPI_current->procCxt);

	SetScanCommandId(internal->_SPI_current->savedId);

	/*
	 * After _SPI_begin_call internal->_SPI_connected == internal->_SPI_curid. Now we are
	 * closing connection to SPI and returning to upper Executor and so
	 * internal->_SPI_connected must be equal to internal->_SPI_curid.
	 */
	internal->_SPI_connected--;
	internal->_SPI_curid--;
	if (internal->_SPI_connected == -1)
	{
		free(internal->_SPI_stack);
		internal->_SPI_stack = NULL;
		internal->_SPI_current = NULL;
	}
	else
	{
		_SPI_connection *new_SPI_stack;

		new_SPI_stack = (_SPI_connection *) realloc(internal->_SPI_stack,
						 (internal->_SPI_connected + 1) * sizeof(_SPI_connection));
		/* This could only fail with a pretty stupid malloc package ... */
		if (new_SPI_stack == NULL)
			elog(ERROR, "Memory exhausted in SPI_finish");
		internal->_SPI_stack = new_SPI_stack;
		internal->_SPI_current = &(internal->_SPI_stack[internal->_SPI_connected]);
	}

	return SPI_OK_FINISH;

}

/*
 * Clean up SPI state at transaction commit or abort (we don't care which).
 */
void
AtEOXact_SPI(void)
{
    SPIInfo*	external = SPI_GetInfo();
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	/*
	 * Note that memory contexts belonging to SPI stack entries will be
	 * freed automatically, so we can ignore them here.  We just need to
	 * restore our static variables to initial state.
	 */
	if (internal->_SPI_stack != NULL)		/* there was abort */
		free(internal->_SPI_stack);
	internal->_SPI_current = internal->_SPI_stack = NULL;
	internal->_SPI_connected = internal->_SPI_curid = -1;
	external->SPI_processed = 0;
	external->SPI_lastoid = InvalidOid;
	external->SPI_tuptable = NULL;
}

void
SPI_push(void)
{
    InternalSPIInfo*  internal = GetInternalSPIInfo();

	internal->_SPI_curid++;
}

void
SPI_pop(void)
{
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	internal->_SPI_curid--;
}

int
SPI_exec(char *src, int tcount)
{
	int			res;

	if (src == NULL || tcount < 0)
		return SPI_ERROR_ARGUMENT;

	res = _SPI_begin_call(true);
	if (res < 0)
		return res;

	res = _SPI_execute(src, tcount, NULL);

	_SPI_end_call(true);
	return res;
}

int
SPI_execp(void *plan, Datum *Values, char *Nulls, int tcount)
{
	int			res;

	if (plan == NULL || tcount < 0)
		return SPI_ERROR_ARGUMENT;

	if (((_SPI_plan *) plan)->nargs > 0 && Values == NULL)
		return SPI_ERROR_PARAM;

	res = _SPI_begin_call(true);
	if (res < 0)
		return res;

	/* copy plan to current (executor) context */
	plan = (void *) _SPI_copy_plan(plan, _SPI_CPLAN_CURCXT);

	res = _SPI_execute_plan((_SPI_plan *) plan, Values, Nulls, tcount);

	_SPI_end_call(true);
	return res;
}

void *
SPI_prepare(char *src, int nargs, Oid *argtypes)
{
    SPIInfo*	external = SPI_GetInfo();
	_SPI_plan  *plan;

	if (src == NULL || nargs < 0 || (nargs > 0 && argtypes == NULL))
	{
		external->SPI_result = SPI_ERROR_ARGUMENT;
		return NULL;
	}

	external->SPI_result = _SPI_begin_call(true);
	if (external->SPI_result < 0)
		return NULL;

	plan = (_SPI_plan *) palloc(sizeof(_SPI_plan));		/* Executor context */
	plan->argtypes = argtypes;
	plan->nargs = nargs;

	external->SPI_result = _SPI_execute(src, 0, plan);

	if (external->SPI_result >= 0)		/* copy plan to procedure context */
		plan = _SPI_copy_plan(plan, _SPI_CPLAN_PROCXT);
	else
		plan = NULL;

	_SPI_end_call(true);

	return (void *) plan;

}

void *
SPI_saveplan(void *plan)
{
    SPIInfo*	external = SPI_GetInfo();
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	_SPI_plan  *newplan;

	if (plan == NULL)
	{
		external->SPI_result = SPI_ERROR_ARGUMENT;
		return NULL;
	}

	external->SPI_result = _SPI_begin_call(false);		/* don't change context */
	if (external->SPI_result < 0)
		return NULL;

	newplan = _SPI_copy_plan((_SPI_plan *) plan, _SPI_CPLAN_TOPCXT);

	internal->_SPI_curid--;
	external->SPI_result = 0;

	return (void *) newplan;

}

int
SPI_freeplan(void *plan)
{
	_SPI_plan  *spiplan = (_SPI_plan *) plan;

	if (plan == NULL)
		return SPI_ERROR_ARGUMENT;

	MemoryContextDelete(spiplan->plancxt);
	return 0;
}

HeapTuple
SPI_copytuple(HeapTuple tuple)
{
    SPIInfo*	external = SPI_GetInfo();
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	MemoryContext oldcxt = NULL;
	HeapTuple	ctuple;

	if (tuple == NULL)
	{
		external->SPI_result = SPI_ERROR_ARGUMENT;
		return NULL;
	}

	if (internal->_SPI_curid + 1 == internal->_SPI_connected)		/* connected */
	{
		if (internal->_SPI_current != &(internal->_SPI_stack[internal->_SPI_curid + 1]))
			elog(FATAL, "SPI: stack corrupted");
		oldcxt = MemoryContextSwitchTo(internal->_SPI_current->savedcxt);
	}

	ctuple = heap_copytuple(tuple);

	if (oldcxt)
		MemoryContextSwitchTo(oldcxt);

	return ctuple;
}

TupleDesc
SPI_copytupledesc(TupleDesc tupdesc)
{
    SPIInfo*	external = SPI_GetInfo();
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	MemoryContext oldcxt = NULL;
	TupleDesc	ctupdesc;

	if (tupdesc == NULL)
	{
		external->SPI_result = SPI_ERROR_ARGUMENT;
		return NULL;
	}

	if (internal->_SPI_curid + 1 == internal->_SPI_connected)		/* connected */
	{
		if (internal->_SPI_current != &(internal->_SPI_stack[internal->_SPI_curid + 1]))
			elog(FATAL, "SPI: stack corrupted");
		oldcxt = MemoryContextSwitchTo(internal->_SPI_current->savedcxt);
	}

	ctupdesc = CreateTupleDescCopy(tupdesc);

	if (oldcxt)
		MemoryContextSwitchTo(oldcxt);

	return ctupdesc;
}

TupleTableSlot *
SPI_copytupleintoslot(HeapTuple tuple, TupleDesc tupdesc)
{
    SPIInfo*	external = SPI_GetInfo();
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	MemoryContext oldcxt = NULL;
	TupleTableSlot *cslot;

	if (tuple == NULL || tupdesc == NULL)
	{
		external->SPI_result = SPI_ERROR_ARGUMENT;
		return NULL;
	}

	if (internal->_SPI_curid + 1 == internal->_SPI_connected)		/* connected */
	{
		if (internal->_SPI_current != &(internal->_SPI_stack[internal->_SPI_curid + 1]))
			elog(FATAL, "SPI: stack corrupted");
		oldcxt = MemoryContextSwitchTo(internal->_SPI_current->savedcxt);
	}


	cslot = MakeTupleTableSlot();
	ExecSetSlotDescriptor(cslot, tupdesc);
	cslot = ExecStoreTuple(tuple, cslot, false);

	if (oldcxt)
		MemoryContextSwitchTo(oldcxt);

	return cslot;
}

HeapTuple
SPI_modifytuple(Relation rel, HeapTuple tuple, int natts, int *attnum,
				Datum *Values, char *Nulls)
{
    SPIInfo*	external = SPI_GetInfo();
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	MemoryContext oldcxt = NULL;
	HeapTuple	mtuple;
	int			numberOfAttributes;
	uint8		infomask;
	Datum	   *v;
	char	   *n;
	bool		isnull;
	int			i;

	if (rel == NULL || tuple == NULL || natts <= 0 || attnum == NULL || Values == NULL)
	{
		external->SPI_result = SPI_ERROR_ARGUMENT;
		return NULL;
	}

	if (internal->_SPI_curid + 1 == internal->_SPI_connected)		/* connected */
	{
		if (internal->_SPI_current != &(internal->_SPI_stack[internal->_SPI_curid + 1]))
			elog(FATAL, "SPI: stack corrupted");
		oldcxt = MemoryContextSwitchTo(internal->_SPI_current->savedcxt);
	}
	external->SPI_result = 0;
	numberOfAttributes = rel->rd_att->natts;
	v = (Datum *) palloc(numberOfAttributes * sizeof(Datum));
	n = (char *) palloc(numberOfAttributes * sizeof(char));

	/* fetch old values and nulls */
	for (i = 0; i < numberOfAttributes; i++)
	{
		v[i] = HeapGetAttr(tuple, i + 1, rel->rd_att, &isnull);
		n[i] = (isnull) ? 'n' : ' ';
	}

	/* replace values and nulls */
	for (i = 0; i < natts; i++)
	{
		if (attnum[i] <= 0 || attnum[i] > numberOfAttributes)
			break;
		v[attnum[i] - 1] = Values[i];
		n[attnum[i] - 1] = (Nulls && Nulls[i] == 'n') ? 'n' : ' ';
	}

	if (i == natts)				/* no errors in *attnum */
	{
		mtuple = heap_formtuple(rel->rd_att, v, n);
		infomask = mtuple->t_data->t_infomask;
		memmove(&(mtuple->t_data->t_oid), &(tuple->t_data->t_oid),
				((char *) &(tuple->t_data->t_hoff) -
				 (char *) &(tuple->t_data->t_oid)));
		mtuple->t_data->t_infomask = infomask;
		mtuple->t_data->t_natts = numberOfAttributes;
	}
	else
	{
		mtuple = NULL;
		external->SPI_result = SPI_ERROR_NOATTRIBUTE;
	}

	pfree(v);
	pfree(n);

	if (oldcxt)
		MemoryContextSwitchTo(oldcxt);

	return mtuple;
}

int
SPI_fnumber(TupleDesc tupdesc, char *fname)
{
	int			res;
	Form_pg_attribute sysatt;

	for (res = 0; res < tupdesc->natts; res++)
	{
		if (namestrcmp(&tupdesc->attrs[res]->attname, fname) == 0)
			return res + 1;
	}

	sysatt = SystemAttributeByName(fname,true);
	if (sysatt != NULL)
		return sysatt->attnum;

	/* SPI_ERROR_NOATTRIBUTE is different from all sys column numbers */
	return SPI_ERROR_NOATTRIBUTE;
}

char *
SPI_fname(TupleDesc tupdesc, int fnumber)
{
    SPIInfo*	external = SPI_GetInfo();
	Form_pg_attribute att;

	external->SPI_result = 0;

	if (fnumber > tupdesc->natts || fnumber == 0 ||
		fnumber <= FirstLowInvalidHeapAttributeNumber)
	{
		external->SPI_result = SPI_ERROR_NOATTRIBUTE;
		return NULL;
	}

	if (fnumber > 0)
		att = tupdesc->attrs[fnumber - 1];
	else
		att = SystemAttributeDefinition(fnumber, true);

	return pstrdup(NameStr(att->attname));
}

char *
SPI_getvalue(HeapTuple tuple, TupleDesc tupdesc, int fnumber)
{
    SPIInfo*	external = SPI_GetInfo();
	Datum		origval,
				val,
				result;
	bool		isnull;
	Oid			typoid,
				foutoid,
				typelem;
	int32		typmod;
	bool		typisvarlena;

	external->SPI_result = 0;

	if (fnumber > tuple->t_data->t_natts || fnumber == 0 ||
		fnumber <= FirstLowInvalidHeapAttributeNumber)
	{
		external->SPI_result = SPI_ERROR_NOATTRIBUTE;
		return NULL;
	}

	origval = HeapGetAttr(tuple, fnumber, tupdesc, &isnull);
	if (isnull)
		return NULL;

	if (fnumber > 0)
	{
		typoid = tupdesc->attrs[fnumber - 1]->atttypid;
		typmod = tupdesc->attrs[fnumber - 1]->atttypmod;
	}
	else
	{
		typoid = (SystemAttributeDefinition(fnumber, true))->atttypid;
		typmod = -1;
	}

	if (!getTypeOutputInfo(typoid, &foutoid, &typelem, &typisvarlena))
	{
		external->SPI_result = SPI_ERROR_NOOUTFUNC;
		return NULL;
	}

	/*
	 * If we have a toasted datum, forcibly detoast it here to avoid
	 * memory leakage inside the type's output routine.
	 */
	if (typisvarlena)
		val = PointerGetDatum(origval);
	else
		val = origval;

	result =(Datum)fmgr(foutoid,
							  val,
							  ObjectIdGetDatum(typelem),
							  Int32GetDatum(typmod));

	/* Clean up detoasted copy, if any */
	if (val != origval)
		pfree(DatumGetPointer(val));

	return DatumGetCString(result);
}

Datum
SPI_getbinval(HeapTuple tuple, TupleDesc tupdesc, int fnumber, bool *isnull)
{
    SPIInfo*	external = SPI_GetInfo();
	external->SPI_result = 0;

	if (fnumber > tuple->t_data->t_natts || fnumber == 0 ||
		fnumber <= FirstLowInvalidHeapAttributeNumber)
	{
		external->SPI_result = SPI_ERROR_NOATTRIBUTE;
		*isnull = true;
		return (Datum) NULL;
	}

	return HeapGetAttr(tuple, fnumber, tupdesc, isnull);
}

char *
SPI_gettype(TupleDesc tupdesc, int fnumber)
{
	Oid			typoid;
	HeapTuple	typeTuple;
	char	   *result;
    SPIInfo*	external = SPI_GetInfo();

	external->SPI_result = 0;

	if (fnumber > tupdesc->natts || fnumber == 0 ||
		fnumber <= FirstLowInvalidHeapAttributeNumber)
	{
		external->SPI_result = SPI_ERROR_NOATTRIBUTE;
		return NULL;
	}

	if (fnumber > 0)
		typoid = tupdesc->attrs[fnumber - 1]->atttypid;
	else
		typoid = (SystemAttributeDefinition(fnumber, true))->atttypid;

	typeTuple = SearchSysCacheTuple(TYPEOID,
							   ObjectIdGetDatum(typoid),
							   0, 0, 0);

	if (!HeapTupleIsValid(typeTuple))
	{
		external->SPI_result = SPI_ERROR_TYPUNKNOWN;
		return NULL;
	}

	result = pstrdup(NameStr(((Form_pg_type) GETSTRUCT(typeTuple))->typname));
/*	ReleaseSysCacheTuple(typeTuple); */
	return result;
}

Oid
SPI_gettypeid(TupleDesc tupdesc, int fnumber)
{
    SPIInfo*	external = SPI_GetInfo();
	external->SPI_result = 0;

	if (fnumber > tupdesc->natts || fnumber == 0 ||
		fnumber <= FirstLowInvalidHeapAttributeNumber)
	{
		external->SPI_result = SPI_ERROR_NOATTRIBUTE;
		return InvalidOid;
	}

	if (fnumber > 0)
		return tupdesc->attrs[fnumber - 1]->atttypid;
	else
		return (SystemAttributeDefinition(fnumber, true))->atttypid;
}

char *
SPI_getrelname(Relation rel)
{
	return pstrdup(RelationGetRelationName(rel));
}

void *
SPI_palloc(Size size)
{
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	MemoryContext oldcxt = NULL;
	void	   *pointer;

	if (internal->_SPI_curid + 1 == internal->_SPI_connected)		/* connected */
	{
		if (internal->_SPI_current != &(internal->_SPI_stack[internal->_SPI_curid + 1]))
			elog(FATAL, "SPI: stack corrupted");
		oldcxt = MemoryContextSwitchTo(internal->_SPI_current->savedcxt);
	}

	pointer = palloc(size);

	if (oldcxt)
		MemoryContextSwitchTo(oldcxt);

	return pointer;
}

void *
SPI_repalloc(void *pointer, Size size)
{
	/* No longer need to worry which context chunk was in... */
	return repalloc(pointer, size);
}

void
SPI_pfree(void *pointer)
{
	/* No longer need to worry which context chunk was in... */
	pfree(pointer);
}

void
SPI_freetuple(HeapTuple tuple)
{
	/* No longer need to worry which context tuple was in... */
	heap_freetuple(tuple);
}

void
SPI_freetuptable(SPITupleTable *tuptable)
{
	if (tuptable != NULL)
		MemoryContextDelete(tuptable->tuptabcxt);
}



/*
 * SPI_cursor_open()
 *
 *	Open a prepared SPI plan as a portal
 */
Portal
SPI_cursor_open(char *name, void *plan, Datum *Values, char *Nulls)
{
    SPIInfo*	external = SPI_GetInfo();
    InternalSPIInfo*  internal = GetInternalSPIInfo();

	_SPI_plan  *spiplan = (_SPI_plan *) plan;
	List	   *qtlist = spiplan->qtlist;
	List	   *ptlist = spiplan->ptlist;
	Query	   *queryTree;
	Plan	   *planTree;
	QueryDesc  *queryDesc;
	EState	   *eState;
	TupleDesc	attinfo;
	MemoryContext oldcontext;
	Portal		portal;
	char		portalname[64];
	int			k;

	internal->unnamed_portal_count = 0;
	/* Ensure that the plan contains only one regular SELECT query */
	if (length(ptlist) != 1)
		elog(ERROR, "cannot open multi-query plan as cursor");
	queryTree = (Query *) lfirst(qtlist);
	planTree = (Plan *) lfirst(ptlist);

	if (queryTree->commandType != CMD_SELECT)
		elog(ERROR, "plan in SPI_cursor_open() is not a SELECT");
	if (queryTree->isPortal)
		elog(ERROR, "plan in SPI_cursor_open() must NOT be a DECLARE already");
	else if (queryTree->into != NULL)
		elog(ERROR, "plan in SPI_cursor_open() must NOT be a SELECT INTO");

	/* Increment CommandCounter to see changes made by now */
	CommandCounterIncrement();

	/* Reset SPI result */
	external->SPI_processed = 0;
	external->SPI_tuptable = NULL;
	internal->_SPI_current->processed = 0;
	internal->_SPI_current->tuptable = NULL;

	if (name == NULL)
	{
		/* Make up a portal name if none given */
		for (;;)
		{
			internal->unnamed_portal_count++;
			if (internal->unnamed_portal_count < 0)
				internal->unnamed_portal_count = 0;
			sprintf(portalname, "<unnamed cursor %d>", internal->unnamed_portal_count);
			if (GetPortalByName(portalname) == NULL)
				break;
		}

		name = portalname;
	}
	else
	{
		/* Ensure the portal doesn't exist already */
		portal = GetPortalByName(name);
		if (portal != NULL)
			elog(ERROR, "cursor \"%s\" already in use", name);
	}

	/* Create the portal */
	portal = CreatePortal(name);
	if (portal == NULL)
		elog(ERROR, "failed to create portal \"%s\"", name);

	/* Switch to portals memory and copy the parsetree and plan to there */
	oldcontext = MemoryContextSwitchTo(PortalGetHeapMemory(portal));
	queryTree = copyObject(queryTree);
	planTree = copyObject(planTree);

	/* Modify the parsetree to be a cursor */
	queryTree->isPortal = true;
	queryTree->into = pstrdup(name);
	queryTree->isBinary = false;

	/* Create the QueryDesc object and the executor state */
	queryDesc = CreateQueryDesc(queryTree, planTree, SPI);
	eState = CreateExecutorState();

	/* If the plan has parameters, put them into the executor state */
	if (spiplan->nargs > 0)
	{
		ParamListInfo paramLI;

		paramLI = (ParamListInfo) palloc((spiplan->nargs + 1) *
										 sizeof(ParamListInfoData));
		MemSet(paramLI, 0, (spiplan->nargs + 1) * sizeof(ParamListInfoData));

		eState->es_param_list_info = paramLI;
		for (k = 0; k < spiplan->nargs; paramLI++, k++)
		{
			paramLI->kind = PARAM_NUM;
			paramLI->id = k + 1;
			paramLI->isnull = (Nulls && Nulls[k] == 'n');
			if (paramLI->isnull)
			{
				/* nulls just copy */
				paramLI->value = Values[k];
			}
			else
			{
				/* pass-by-ref values must be copied into portal context */
				int16		paramTypLen;
				bool		paramTypByVal;

                paramTypLen = get_typlen(spiplan->argtypes[k]);
                paramTypByVal = get_typbyval(spiplan->argtypes[k]);
				paramLI->value = datumCopy(Values[k],spiplan->argtypes[k],
										   paramTypByVal, paramTypLen);
			}
		}
		paramLI->kind = PARAM_INVALID;
	}
	else
		eState->es_param_list_info = NULL;

	/* Start the executor */
	attinfo = ExecutorStart(queryDesc, eState);

	/* Put all the objects into the portal */
	PortalSetQuery(portal, queryDesc, attinfo, eState, PortalCleanup);

	/* Switch back to the callers memory context */
	MemoryContextSwitchTo(oldcontext);

	/* Return the created portal */
	return portal;
}


/*
 * SPI_cursor_find()
 *
 *	Find the portal of an existing open cursor
 */
Portal
SPI_cursor_find(char *name)
{
	return GetPortalByName(name);
}


/*
 * SPI_cursor_fetch()
 *
 *	Fetch rows in a cursor
 */
void
SPI_cursor_fetch(Portal portal, bool forward, int count)
{
	_SPI_cursor_operation(portal, forward, count, SPI);
}


/*
 * SPI_cursor_move()
 *
 *	Move in a cursor
 */
void
SPI_cursor_move(Portal portal, bool forward, int count)
{
	_SPI_cursor_operation(portal, forward, count, None);
}


/*
 * SPI_cursor_close()
 *
 *	Close a cursor
 */
void
SPI_cursor_close(Portal portal)
{
	if (!PortalIsValid(portal))
		elog(ERROR, "invalid portal in SPI cursor operation");

	PortalDrop(portal);
}

/* =================== private functions =================== */

/*
 * spi_printtup
 *		store tuple retrieved by Executor into SPITupleTable
 *		of current SPI procedure
 *
 */
void
spi_printtup(HeapTuple tuple, TupleDesc tupdesc, DestReceiver *self)
{
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	SPITupleTable *tuptable;
	MemoryContext oldcxt;
	MemoryContext tuptabcxt;

	/*
	 * When called by Executor internal->_SPI_curid expected to be equal to
	 * internal->_SPI_connected
	 */
	if (internal->_SPI_curid != internal->_SPI_connected || internal->_SPI_connected < 0)
		elog(FATAL, "SPI: improper call to spi_printtup");
	if (internal->_SPI_current != &(internal->_SPI_stack[internal->_SPI_curid]))
		elog(FATAL, "SPI: stack corrupted in spi_printtup");

	oldcxt = _SPI_procmem();	/* switch to procedure memory context */

	tuptable = internal->_SPI_current->tuptable;
	if (tuptable == NULL)
	{
		tuptabcxt = AllocSetContextCreate(MemoryContextGetCurrentContext(),
										  "SPI TupTable",
										  ALLOCSET_DEFAULT_MINSIZE,
										  ALLOCSET_DEFAULT_INITSIZE,
										  ALLOCSET_DEFAULT_MAXSIZE);
		MemoryContextSwitchTo(tuptabcxt);

		internal->_SPI_current->tuptable = tuptable = (SPITupleTable *)
			palloc(sizeof(SPITupleTable));
		tuptable->tuptabcxt = tuptabcxt;
		tuptable->alloced = tuptable->free = 128;
		tuptable->vals = (HeapTuple *) palloc(tuptable->alloced * sizeof(HeapTuple));
		tuptable->tupdesc = CreateTupleDescCopy(tupdesc);
	}
	else
	{
		MemoryContextSwitchTo(tuptable->tuptabcxt);

		if (tuptable->free == 0)
		{
			tuptable->free = 256;
			tuptable->alloced += tuptable->free;
			tuptable->vals = (HeapTuple *) repalloc(tuptable->vals,
								  tuptable->alloced * sizeof(HeapTuple));
		}
	}

	tuptable->vals[tuptable->alloced - tuptable->free] = heap_copytuple(tuple);
	(tuptable->free)--;

	MemoryContextSwitchTo(oldcxt);
	return;
}

/*
 * Static functions
 */

static int
_SPI_execute(char *src, int tcount, _SPI_plan *plan)
{
    SPIInfo*	external = SPI_GetInfo();
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	List	   *queryTree_list;
	List	   *planTree_list;
	List	   *queryTree_list_item;
	QueryDesc  *qdesc;
	Query	   *queryTree;
	Plan	   *planTree;
	EState	   *state;
	int			nargs = 0;
	Oid		   *argtypes = NULL;
	int			res = 0;
	bool		islastquery;
    char*		fake[1];

	/* Increment CommandCounter to see changes made by now */
	CommandCounterIncrement();

	external->SPI_processed = 0;
	external->SPI_lastoid = InvalidOid;
	external->SPI_tuptable = NULL;
	internal->_SPI_current->tuptable = NULL;
	internal->_SPI_current->qtlist = NULL;

	if (plan)
	{
		nargs = plan->nargs;
		argtypes = plan->argtypes;
	}

	queryTree_list = pg_parse_and_rewrite(src, argtypes,fake, nargs,false);

	internal->_SPI_current->qtlist = queryTree_list;

	planTree_list = NIL;

	foreach(queryTree_list_item, queryTree_list)
	{
		queryTree = (Query *) lfirst(queryTree_list_item);
		islastquery = (lnext(queryTree_list_item) == NIL);

		planTree = pg_plan_query(queryTree);
		planTree_list = lappend(planTree_list, planTree);

		if (queryTree->commandType == CMD_UTILITY)
		{
			if (nodeTag(queryTree->utilityStmt) == T_CopyStmt)
			{
				CopyStmt   *stmt = (CopyStmt *) (queryTree->utilityStmt);

				if (stmt->filename == NULL)
					return SPI_ERROR_COPY;
			}
			else if (nodeTag(queryTree->utilityStmt) == T_ClosePortalStmt ||
					 nodeTag(queryTree->utilityStmt) == T_FetchStmt)
				return SPI_ERROR_CURSOR;
			else if (nodeTag(queryTree->utilityStmt) == T_TransactionStmt)
				return SPI_ERROR_TRANSACTION;
			res = SPI_OK_UTILITY;
			if (plan == NULL)
			{
				ProcessUtility(queryTree->utilityStmt, None);
				if (!islastquery)
					CommandCounterIncrement();
				else
					return res;
			}
			else if (islastquery)
				break;
		}
		else if (plan == NULL)
		{
			qdesc = CreateQueryDesc(queryTree, planTree,
									islastquery ? SPI : None);
			state = CreateExecutorState();
			res = _SPI_pquery(qdesc, state, islastquery ? tcount : 0);
			if (res < 0 || islastquery)
				return res;
			CommandCounterIncrement();
		}
		else
		{
			qdesc = CreateQueryDesc(queryTree, planTree,
									islastquery ? SPI : None);
			res = _SPI_pquery(qdesc, NULL, islastquery ? tcount : 0);
			if (res < 0)
				return res;
			if (islastquery)
				break;
		}
	}

	if (plan)
	{
		plan->qtlist = queryTree_list;
		plan->ptlist = planTree_list;
	}

	return res;
}

static int
_SPI_execute_plan(_SPI_plan *plan, Datum *Values, char *Nulls, int tcount)
{
    SPIInfo*	external = SPI_GetInfo();
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	List	   *queryTree_list = plan->qtlist;
	List	   *planTree_list = plan->ptlist;
	List	   *queryTree_list_item;
	QueryDesc  *qdesc;
	Query	   *queryTree;
	Plan	   *planTree;
	EState	   *state;
	int			nargs = plan->nargs;
	int			res = 0;
	bool		islastquery;
	int			k;

	/* Increment CommandCounter to see changes made by now */
	CommandCounterIncrement();

	external->SPI_processed = 0;
	external->SPI_lastoid = InvalidOid;
	external->SPI_tuptable = NULL;
	internal->_SPI_current->tuptable = NULL;
	internal->_SPI_current->qtlist = NULL;

	foreach(queryTree_list_item, queryTree_list)
	{
		queryTree = (Query *) lfirst(queryTree_list_item);
		planTree = lfirst(planTree_list);
		planTree_list = lnext(planTree_list);
		islastquery = (planTree_list == NIL);	/* assume lists are same
												 * len */

		if (queryTree->commandType == CMD_UTILITY)
		{
			ProcessUtility(queryTree->utilityStmt, None);
			if (!islastquery)
				CommandCounterIncrement();
			else
				return SPI_OK_UTILITY;
		}
		else
		{
			qdesc = CreateQueryDesc(queryTree, planTree,
									islastquery ? SPI : None);
			state = CreateExecutorState();
			if (nargs > 0)
			{
				ParamListInfo paramLI;

				paramLI = (ParamListInfo) palloc((nargs + 1) *
												 sizeof(ParamListInfoData));
				MemSet(paramLI, 0, (nargs + 1) * sizeof(ParamListInfoData));

				state->es_param_list_info = paramLI;
				for (k = 0; k < plan->nargs; paramLI++, k++)
				{
					paramLI->kind = PARAM_NUM;
					paramLI->id = k + 1;
					paramLI->isnull = (Nulls && Nulls[k] == 'n');
					paramLI->value = Values[k];
				}
				paramLI->kind = PARAM_INVALID;
			}
			else
				state->es_param_list_info = NULL;
			res = _SPI_pquery(qdesc, state, islastquery ? tcount : 0);
			if (res < 0 || islastquery)
				return res;
			CommandCounterIncrement();
		}
	}

	return res;
}

static int
_SPI_pquery(QueryDesc *queryDesc, EState *state, int tcount)
{
    SPIInfo*	external = SPI_GetInfo();
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	Query	   *parseTree = queryDesc->parsetree;
	int			operation = queryDesc->operation;
	CommandDest dest = queryDesc->dest;
	bool		isRetrieveIntoPortal = false;
	int			res;
	Oid			save_lastoid;

	switch (operation)
	{
		case CMD_SELECT:
			res = SPI_OK_SELECT;
			if (parseTree->isPortal)
			{
				isRetrieveIntoPortal = true;
				parseTree->isBinary = false;	/* */

				return SPI_ERROR_CURSOR;

			}
			else if (parseTree->into != NULL)	/* select into table */
			{
				res = SPI_OK_SELINTO;
				queryDesc->dest = None; /* */
			}
			break;
		case CMD_PUT:
		case CMD_INSERT:
			res = SPI_OK_INSERT;
			break;
		case CMD_DELETE:
			res = SPI_OK_DELETE;
			break;
		case CMD_UPDATE:
			res = SPI_OK_UPDATE;
			break;
		default:
			return SPI_ERROR_OPUNKNOWN;
	}

	if (state == NULL)			/* plan preparation */
		return res;
#ifdef SPI_EXECUTOR_STATS
	if (ShowExecutorStats)
		ResetUsage();
#endif
	ExecutorStart(queryDesc, state);

	/*
	 * Don't work currently --- need to rearrange callers so that we
	 * prepare the portal before doing CreateExecutorState() etc. See
	 * pquery.c for the correct order of operations.
	 */
	if (isRetrieveIntoPortal)
		elog(FATAL, "SPI_select: retrieve into portal not implemented");

	ExecutorRun(queryDesc, state, EXEC_FOR,NULL,NULL /*(long) tcount*/);

	internal->_SPI_current->processed = state->es_processed;
	save_lastoid = state->es_lastoid;

	if (operation == CMD_SELECT && queryDesc->dest == SPI)
	{
		if (_SPI_checktuples())
			elog(FATAL, "SPI_select: # of processed tuples check failed");
	}

	ExecutorEnd(queryDesc, state);

#ifdef SPI_EXECUTOR_STATS
	if (ShowExecutorStats)
		ShowUsage("SPI EXECUTOR STATS");
#endif

	if (dest == SPI)
	{
		external->SPI_processed = internal->_SPI_current->processed;
		external->SPI_lastoid = save_lastoid;
		external->SPI_tuptable = internal->_SPI_current->tuptable;
	}
	queryDesc->dest = dest;

	return res;

}

/*
 * _SPI_cursor_operation()
 *
 *	Do a FETCH or MOVE in a cursor
 */
static void
_SPI_cursor_operation(Portal portal, bool forward, int count,
					  CommandDest dest)
{
    SPIInfo*	external = SPI_GetInfo();
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	QueryDesc  *querydesc;
	EState	   *estate;
	MemoryContext oldcontext;
	CommandId	savedId;
	CommandDest olddest;

	/* Check that the portal is valid */
	if (!PortalIsValid(portal))
		elog(ERROR, "invalid portal in SPI cursor operation");

	/* Push the SPI stack */
	_SPI_begin_call(true);

	/* Reset the SPI result */
	external->SPI_processed = 0;
	external->SPI_tuptable = NULL;
	internal->_SPI_current->processed = 0;
	internal->_SPI_current->tuptable = NULL;

	/* Switch to the portals memory context */
	oldcontext = MemoryContextSwitchTo(PortalGetHeapMemory(portal));

	querydesc = PortalGetQueryDesc(portal);
	estate = PortalGetState(portal);

	/* Save the queries command destination and set it to SPI (for fetch) */
	/* or None (for move) */
	olddest = querydesc->dest;
	querydesc->dest = dest;

	/*
	 * Restore the scanCommandId that was current when the cursor was
	 * opened.  This ensures that we see the same tuples throughout the
	 * execution of the cursor.
	 */
	savedId = GetScanCommandId();
	SetScanCommandId(PortalGetCommandId(portal));

	/* Run the executor like PerformPortalFetch and remember states */
	if (forward)
	{
		if (!portal->atEnd)
		{
			ExecutorRun(querydesc, estate, EXEC_FOR, NULL,NULL/*(long) count*/);
			internal->_SPI_current->processed = estate->es_processed;
			if (estate->es_processed > 0)
				portal->atStart = false;
			if (count <= 0 || (int) estate->es_processed < count)
				portal->atEnd = true;
		}
	}
	else
	{
		if (!portal->atStart)
		{
			ExecutorRun(querydesc, estate, EXEC_BACK,NULL,NULL /*(long) count*/);
			internal->_SPI_current->processed = estate->es_processed;
			if (estate->es_processed > 0)
				portal->atEnd = false;
			if (count <= 0 || estate->es_processed < count)
				portal->atStart = true;
		}
	}

	/*
	 * Restore outer command ID.
	 */
	SetScanCommandId(savedId);

	/* Restore the old command destination and switch back to callers */
	/* memory context */
	querydesc->dest = olddest;
	MemoryContextSwitchTo(oldcontext);

	if (dest == SPI && _SPI_checktuples())
		elog(FATAL, "SPI_fetch: # of processed tuples check failed");

	/* Put the result into place for access by caller */
	external->SPI_processed = internal->_SPI_current->processed;
	external->SPI_tuptable = internal->_SPI_current->tuptable;

	/* Pop the SPI stack */
	_SPI_end_call(true);
}


static MemoryContext
_SPI_execmem()
{
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	return MemoryContextSwitchTo(internal->_SPI_current->execCxt);
}

static MemoryContext
_SPI_procmem()
{
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	return MemoryContextSwitchTo(internal->_SPI_current->procCxt);
}

/*
 * _SPI_begin_call
 *
 */
static int
_SPI_begin_call(bool execmem)
{
	InternalSPIInfo*  internal = GetInternalSPIInfo();
	if (internal->_SPI_curid + 1 != internal->_SPI_connected)
		return SPI_ERROR_UNCONNECTED;
	internal->_SPI_curid++;
	if (internal->_SPI_current != &(internal->_SPI_stack[internal->_SPI_curid]))
		elog(FATAL, "SPI: stack corrupted");

	if (execmem)				/* switch to the Executor memory context */
		_SPI_execmem();

	return 0;
}

static int
_SPI_end_call(bool procmem)
{
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	/*
	 * We' returning to procedure where internal->_SPI_curid == internal->_SPI_connected - 1
	 */
	internal->_SPI_curid--;

	internal->_SPI_current->qtlist = NULL;

	if (procmem)				/* switch to the procedure memory context */
	{
		_SPI_procmem();
		/* and free Executor memory */
		MemoryContextResetAndDeleteChildren(internal->_SPI_current->execCxt);
	}

	return 0;
}

static bool
_SPI_checktuples(void)
{
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	uint32		processed = internal->_SPI_current->processed;
	SPITupleTable *tuptable = internal->_SPI_current->tuptable;
	bool		failed = false;

	if (processed == 0)
	{
		if (tuptable != NULL)
			failed = true;
	}
	else
	{
		/* some tuples were processed */
		if (tuptable == NULL)	/* spi_printtup was not called */
			failed = true;
		else if (processed != (tuptable->alloced - tuptable->free))
			failed = true;
	}

	return failed;
}

static _SPI_plan *
_SPI_copy_plan(_SPI_plan *plan, int location)
{
    InternalSPIInfo*  internal = GetInternalSPIInfo();
	_SPI_plan  *newplan;
	MemoryContext oldcxt;
	MemoryContext plancxt;
	MemoryContext parentcxt;

	/* Determine correct parent for the plan's memory context */
	if (location == _SPI_CPLAN_PROCXT)
		parentcxt = internal->_SPI_current->procCxt;
	else if (location == _SPI_CPLAN_TOPCXT)
		parentcxt = MemoryContextGetTopContext();
	else
		parentcxt = MemoryContextGetCurrentContext();

	/*
	 * Create a memory context for the plan.  We don't expect the plan to
	 * be very large, so use smaller-than-default alloc parameters.
	 */
	plancxt = AllocSetContextCreate(parentcxt,
									"SPI Plan",
									1024,
									1024,
									ALLOCSET_DEFAULT_MAXSIZE);
	oldcxt = MemoryContextSwitchTo(plancxt);

	/* Copy the SPI plan into its own context */
	newplan = (_SPI_plan *) palloc(sizeof(_SPI_plan));
	newplan->plancxt = plancxt;
	newplan->qtlist = (List *) copyObject(plan->qtlist);
	newplan->ptlist = (List *) copyObject(plan->ptlist);
	newplan->nargs = plan->nargs;
	if (plan->nargs > 0)
	{
		newplan->argtypes = (Oid *) palloc(plan->nargs * sizeof(Oid));
		memcpy(newplan->argtypes, plan->argtypes, plan->nargs * sizeof(Oid));
	}
	else
		newplan->argtypes = NULL;

	MemoryContextSwitchTo(oldcxt);

	return newplan;
}

TupleTableSlot *
MakeTupleTableSlot(void)
{
	TupleTableSlot *slot = makeNode(TupleTableSlot);

	/* This should match ExecAllocTableSlot() */
	slot->val = (HeapTuple) NULL;
	slot->ttc_descIsNew = true;
	slot->ttc_tupleDescriptor = (TupleDesc) NULL;

	return slot;
}

/*
 * Note:
 *		Should the system special case these attributes in the future?
 *		Advantage:	consume much less space in the ATTRIBUTE relation.
 *		Disadvantage:  special cases will be all over the place.
 */

static FormData_pg_attribute a1 = {
	0, {"ctid"}, TIDOID, 0, sizeof(ItemPointerData),
	SelfItemPointerAttributeNumber, 0, -1, -1,
	false, 'p', false, 'i', false, false
};

static FormData_pg_attribute a2 = {
	0, {"oid"}, OIDOID, 0, sizeof(Oid),
	ObjectIdAttributeNumber, 0, -1, -1,
	true, 'p', false, 'i', false, false
};

static FormData_pg_attribute a3 = {
	0, {"xmin"}, XIDOID, 0, sizeof(TransactionId),
	MinTransactionIdAttributeNumber, 0, -1, -1,
	true, 'p', false, 'i', false, false
};

static FormData_pg_attribute a4 = {
	0, {"cmin"}, CIDOID, 0, sizeof(CommandId),
	MinCommandIdAttributeNumber, 0, -1, -1,
	true, 'p', false, 'i', false, false
};

static FormData_pg_attribute a5 = {
	0, {"xmax"}, XIDOID, 0, sizeof(TransactionId),
	MaxTransactionIdAttributeNumber, 0, -1, -1,
	true, 'p', false, 'i', false, false
};

static FormData_pg_attribute a6 = {
	0, {"cmax"}, CIDOID, 0, sizeof(CommandId),
	MaxCommandIdAttributeNumber, 0, -1, -1,
	true, 'p', false, 'i', false, false
};


static Form_pg_attribute SysAtt[] = {&a1, &a2, &a3, &a4, &a5, &a6};


Form_pg_attribute
SystemAttributeByName(const char *attname, bool relhasoids)
{
	int			j;

	for (j = 0; j < (int) lengthof(SysAtt); j++)
	{
		Form_pg_attribute att = SysAtt[j];

		if (relhasoids || att->attnum != ObjectIdAttributeNumber)
		{
			if (strcmp(NameStr(att->attname), attname) == 0)
				return att;
		}
	}

	return NULL;
}

/*
 * This function returns a Form_pg_attribute pointer for a system attribute.
 * Note that we elog if the presented attno is invalid.
 */
Form_pg_attribute
SystemAttributeDefinition(AttrNumber attno, bool relhasoids)
{
	if (attno >= 0 || attno < -(int) lengthof(SysAtt))
		elog(ERROR, "SystemAttributeDefinition: invalid attribute number %d",
			 attno);
	if (attno == ObjectIdAttributeNumber && !relhasoids)
		elog(ERROR, "SystemAttributeDefinition: invalid attribute number %d",
			 attno);
	return SysAtt[-attno - 1];
}

SPIInfo*
SPI_GetInfo(void)
{
    SPIGlobal* global = GetEnvSpace(spi_id);
    if ( global == NULL ){
        global = AllocateEnvSpace(spi_id,sizeof(SPIGlobal));
    }
    return &global->info;
}

InternalSPIInfo*
GetInternalSPIInfo(void)
{
    SPIGlobal* global = GetEnvSpace(spi_id);
    if ( global == NULL ){
        global = AllocateEnvSpace(spi_id,sizeof(SPIGlobal));
    }
    return &global->internal;
}

/* ----------------
 *		getTypeOutputInfo -- get info needed for printing values of a type
 * ----------------
 */
bool
getTypeOutputInfo(Oid type, Oid *typOutput, Oid *typElem,
				  bool *typIsVarlena)
{
	HeapTuple	typeTuple;
	Form_pg_type pt;

	typeTuple = SearchSysCacheTuple(TYPEOID,
							   ObjectIdGetDatum(type),
							   0, 0, 0);
	if (!HeapTupleIsValid(typeTuple))
		elog(ERROR, "getTypeOutputInfo: Cache lookup of type %lu failed", type);
	pt = (Form_pg_type) GETSTRUCT(typeTuple);

	*typOutput = pt->typoutput;
	*typElem = pt->typelem;
	*typIsVarlena = (!pt->typbyval) && (pt->typlen == -1);
	return OidIsValid(*typOutput);
}


