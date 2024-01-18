/*-------------------------------------------------------------------------
 *
 * index.c
 *	  code to create and destroy POSTGRES index relations
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/catalog/index.c,v 1.4 2007/03/20 03:07:40 synmscott Exp $
 *
 *
 * INTERFACE ROUTINES
 *		index_create()			- Create a cataloged index relation
 *		index_drop()			- Removes index relation from catalogs
 *
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "env/env.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/istrat.h"
#include "access/xact.h"
#include "catalog/catname.h"
#include "catalog/heap.h"
#include "catalog/index.h"
#include "catalog/indexing.h"
#include "catalog/pg_index.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "commands/comment.h"
#include "executor/executor.h"
#include "miscadmin.h"
#include "optimizer/clauses.h"
#include "optimizer/prep.h"
#include "parser/parse_func.h"
#include "storage/smgr.h"
#include "utils/builtins.h"
#include "utils/catcache.h"
#include "utils/relcache.h"
#include "utils/syscache.h"
#include "utils/temprel.h"
#include "utils/inval.h"
#include "env/freespace.h"
#include "env/poolsweep.h"
#include "env/dbwriter.h"

#ifdef GLOBALCACHE
extern MemoryContext CacheCxt
#else
#define CacheCxt GetEnv()->CacheCxt
#endif

/*
 * macros used in guessing how many tuples are on a page.
 */
#define AVG_ATTR_SIZE 8
#define NTUPLES_PER_PAGE(natts) \
	((BLCKSZ - MAXALIGN(sizeof (PageHeaderData))) / \
	((natts) * AVG_ATTR_SIZE + MAXALIGN(sizeof(HeapTupleHeaderData))))
#define MORE_THAN_THE_NUMBER_OF_CATALOGS 256


/*  for bootstrapping only */
static void index_register(char *heap,
               char *ind,
               int natts,
               AttrNumber *attnos,
               uint16 nparams,
               Datum *params,
               FuncIndexInfo *finfo,
               PredInfo *predInfo);
/*
 *    At bootstrap time, we first declare all the indices to be built, and
 *    then build them.  The IndexList structure stores enough information
 *    to allow us to build the indices after they've been declared.
 */

typedef struct _IndexList
{
    char       *il_heap;
    char       *il_ind;
    int            il_natts;
    AttrNumber *il_attnos;
    uint16        il_nparams;
    Datum       *il_params;
    FuncIndexInfo *il_finfo;
    PredInfo   *il_predInfo;
    struct _IndexList *il_next;
} IndexList;

static IndexList *ILHead = (IndexList *) NULL;
static MemoryContext nogc = NULL; /* special no-gc mem
                                                 * context */
/*  END for bootstrapping only */

/* non-export function prototypes */
static Oid GetHeapRelationOid(char *heapRelationName, char *indexRelationName,
				   bool istemp);
static TupleDesc BuildFuncTupleDesc(FuncIndexInfo *funcInfo);
static TupleDesc ConstructTupleDescriptor(Oid heapoid, Relation heapRelation,
				  List *attributeList, int numatts, AttrNumber *attNums);

static void ConstructIndexReldesc(Relation indexRelation, Oid amoid);
static Oid	UpdateRelationRelation(Relation indexRelation, char *temp_relname);
static void InitializeAttributeOids(Relation indexRelation,
						int numatts, Oid indexoid);
static void AppendAttributeTuples(Relation indexRelation, int numatts);
static void UpdateIndexRelation(Oid indexoid, Oid heapoid,
					FuncIndexInfo *funcInfo, int natts,
					AttrNumber *attNums, Oid *classOids, Node *predicate,
		   List *attributeList, bool isdeferred, bool islossy, bool unique, bool primary);
static void DefaultBuild(Relation heapRelation, Relation indexRelation,
			 int numberOfAttributes, AttrNumber *attributeNumber,
			 IndexStrategy indexStrategy, uint16 parameterCount,
		Datum *parameter, FuncIndexInfoPtr funcInfo, PredInfo *predInfo);
static bool BootstrapAlreadySeen(Oid id);


/*
static bool reindexing = false;
*/
#define reindexing GetIndexGlobals()->reindexing
extern bool
SetReindexProcessing(bool reindexmode)
{
	bool		old = reindexing;

	reindexing = reindexmode;
	return old;
}
extern bool
IsReindexProcessing(void)
{
	return reindexing;
}
extern void
ResetReindexProcessing(void)
{
	SetReindexProcessing(false);
}

/* ----------------------------------------------------------------
 *	  sysatts is a structure containing attribute tuple forms
 *	  for system attributes (numbered -1, -2, ...).  This really
 *	  should be generated or eliminated or moved elsewhere. -cim 1/19/91
 *
 * typedef struct FormData_pg_attribute {
 *		Oid				attrelid;
 *		NameData		attname;
 *		Oid				atttypid;
 *		uint32			attnvals;
 *		int16			attlen;
 *		AttrNumber		attnum;
 *		uint32			attnelems;
 *		int32			attcacheoff;
 *		int32			atttypmod;
 *		bool			attbyval;
 *		bool			attisset;
 *		char			attalign;
 *		bool			attnotnull;
 *		bool			atthasdef;
 * } FormData_pg_attribute;
 *
 * ----------------------------------------------------------------
 */
static FormData_pg_attribute sysatts[] = {
	{0, {"ctid"}, TIDOID, 0, 6, -1, 0, -1, -1, '\0', 'p', '\0', 'i', '\0', '\0'},
	{0, {"oid"}, OIDOID, 0, sizeof(Oid), -2, 0, -1, -1, '\001', 'p', '\0', 'i', '\0', '\0'},
	{0, {"xmin"}, XIDOID, 0, sizeof(TransactionId), -3, 0, -1, -1, '\0', 'p', '\0', 'd', '\0', '\0'},
	{0, {"cmin"}, CIDOID, 0, sizeof(CommandId), -4, 0, -1, -1, '\001', 'p', '\0', 'i', '\0', '\0'},
	{0, {"xmax"}, XIDOID, 0, sizeof(TransactionId), -5, 0, -1, -1, '\0', 'p', '\0', 'd', '\0', '\0'},
	{0, {"cmax"}, CIDOID, 0, sizeof(CommandId), -6, 0, -1, -1, '\001', 'p', '\0', 'i', '\0', '\0'},
};

/* ----------------------------------------------------------------
 *		GetHeapRelationOid
 * ----------------------------------------------------------------
 */
static Oid
GetHeapRelationOid(char *heapRelationName, char *indexRelationName, bool istemp)
{
	Oid			indoid;
	Oid			heapoid;


	indoid = RelnameFindRelid(indexRelationName);

	if ((!istemp && OidIsValid(indoid)) ||
		(istemp && get_temp_rel_by_username(indexRelationName) != NULL))
		elog(ERROR, "Cannot create index: '%s' already exists",
			 indexRelationName);

	heapoid = RelnameFindRelid(heapRelationName);

	if (!OidIsValid(heapoid))
		elog(ERROR, "Cannot create index on '%s': relation does not exist",
			 heapRelationName);

	return heapoid;
}

static TupleDesc
BuildFuncTupleDesc(FuncIndexInfo *funcInfo)
{
	HeapTuple	tuple;
	TupleDesc	funcTupDesc;
	Oid			retType;
	char	   *funcname;
	int4		nargs;
	Oid		   *argtypes;

	/*
	 * Allocate and zero a tuple descriptor.
	 */
	funcTupDesc = CreateTemplateTupleDesc(1);
	funcTupDesc->attrs[0] = (Form_pg_attribute) palloc(ATTRIBUTE_TUPLE_SIZE);
	MemSet(funcTupDesc->attrs[0], 0, ATTRIBUTE_TUPLE_SIZE);

	/*
	 * Lookup the function for the return type.
	 */
	funcname = FIgetname(funcInfo);
	nargs = FIgetnArgs(funcInfo);
	argtypes = FIgetArglist(funcInfo);
	tuple = SearchSysCacheTuple(PROCNAME,
								PointerGetDatum(funcname),
								Int32GetDatum(nargs),
								PointerGetDatum(argtypes),
								0);

	if (!HeapTupleIsValid(tuple))
		func_error("BuildFuncTupleDesc", funcname, nargs, argtypes, NULL);

	retType = ((Form_pg_proc) GETSTRUCT(tuple))->prorettype;

	/*
	 * Look up the return type in pg_type for the type length.
	 */
	tuple = SearchSysCacheTuple(TYPEOID,
								ObjectIdGetDatum(retType),
								0, 0, 0);
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "Function %s return type does not exist", FIgetname(funcInfo));

	/*
	 * Assign some of the attributes values. Leave the rest as 0.
	 */
	funcTupDesc->attrs[0]->attlen = ((Form_pg_type) GETSTRUCT(tuple))->typlen;
	funcTupDesc->attrs[0]->atttypid = retType;
	funcTupDesc->attrs[0]->attnum = 1;
	funcTupDesc->attrs[0]->attbyval = ((Form_pg_type) GETSTRUCT(tuple))->typbyval;
	funcTupDesc->attrs[0]->attcacheoff = -1;
	funcTupDesc->attrs[0]->atttypmod = -1;
	funcTupDesc->attrs[0]->attstorage = 'p';
	funcTupDesc->attrs[0]->attalign = ((Form_pg_type) GETSTRUCT(tuple))->typalign;

	/*
	 * make the attributes name the same as the functions
	 */
	namestrcpy(&funcTupDesc->attrs[0]->attname, funcname);

	return funcTupDesc;
}

/* ----------------------------------------------------------------
 *		ConstructTupleDescriptor
 * ----------------------------------------------------------------
 */
static TupleDesc
ConstructTupleDescriptor(Oid heapoid,
						 Relation heapRelation,
						 List *attributeList,
						 int numatts,
						 AttrNumber *attNums)
{
	TupleDesc	heapTupDesc;
	TupleDesc	indexTupDesc;
	IndexElem  *IndexKey;
	TypeName   *IndexKeyType;
	AttrNumber	atnum;			/* attributeNumber[attributeOffset] */
	AttrNumber	atind;
	int			natts;			/* Form_pg_class->relnatts */
	char	   *from;			/* used to simplify memcpy below */
	char	   *to;				/* used to simplify memcpy below */
	int			i;

	/* ----------------
	 *	allocate the new tuple descriptor
	 * ----------------
	 */
	natts = RelationGetForm(heapRelation)->relnatts;

	indexTupDesc = CreateTemplateTupleDesc(numatts);

	/* ----------------
	 *
	 * ----------------
	 */

	/* ----------------
	 *	  for each attribute we are indexing, obtain its attribute
	 *	  tuple form from either the static table of system attribute
	 *	  tuple forms or the relation tuple descriptor
	 * ----------------
	 */
	for (i = 0; i < numatts; i += 1)
	{

		/* ----------------
		 *	 get the attribute number and make sure it's valid
		 * ----------------
		 */
		atnum = attNums[i];
		if (atnum > natts)
			elog(ERROR, "Cannot create index: attribute %d does not exist",
				 atnum);
		if (attributeList)
		{
			IndexKey = (IndexElem *) lfirst(attributeList);
			IndexKeyType = IndexKey->typename;
			attributeList = lnext(attributeList);
		}
		else
			IndexKeyType = NULL;

		indexTupDesc->attrs[i] = (Form_pg_attribute) palloc(ATTRIBUTE_TUPLE_SIZE);

		/* ----------------
		 *	 determine which tuple descriptor to copy
		 * ----------------
		 */
		if (!AttrNumberIsForUserDefinedAttr(atnum))
		{

			/* ----------------
			 *	  here we are indexing on a system attribute (-1...-12)
			 *	  so we convert atnum into a usable index 0...11 so we can
			 *	  use it to dereference the array sysatts[] which stores
			 *	  tuple descriptor information for system attributes.
			 * ----------------
			 */
			if (atnum <= FirstLowInvalidHeapAttributeNumber || atnum >= 0)
				elog(ERROR, "Cannot create index on system attribute: attribute number out of range (%d)", atnum);
			atind = (-atnum) - 1;

			from = (char *) (&sysatts[atind]);

		}
		else
		{
			/* ----------------
			 *	  here we are indexing on a normal attribute (1...n)
			 * ----------------
			 */
			heapTupDesc = RelationGetDescr(heapRelation);
			atind = AttrNumberGetAttrOffset(atnum);

			from = (char *) (heapTupDesc->attrs[atind]);
		}

		/* ----------------
		 *	 now that we've determined the "from", let's copy
		 *	 the tuple desc data...
		 * ----------------
		 */

		to = (char *) (indexTupDesc->attrs[i]);
		memcpy(to, from, ATTRIBUTE_TUPLE_SIZE);

		((Form_pg_attribute) to)->attnum = i + 1;

		((Form_pg_attribute) to)->attnotnull = false;
		((Form_pg_attribute) to)->atthasdef = false;
		((Form_pg_attribute) to)->attcacheoff = -1;
		((Form_pg_attribute) to)->atttypmod = -1;
		((Form_pg_attribute) to)->attalign = 'i';

		/*
		 * if the keytype is defined, we need to change the tuple form's
		 * atttypid & attlen field to match that of the key's type
		 */
		if (IndexKeyType != NULL)
		{
			HeapTuple	tup;

			tup = SearchSysCacheTuple(TYPENAME,
									  PointerGetDatum(IndexKeyType->name),
									  0, 0, 0);
			if (!HeapTupleIsValid(tup))
				elog(ERROR, "create index: type '%s' undefined",
					 IndexKeyType->name);
			((Form_pg_attribute) to)->atttypid = tup->t_data->t_oid;
			((Form_pg_attribute) to)->attbyval =
				((Form_pg_type) GETSTRUCT(tup))->typbyval;
			((Form_pg_attribute) to)->attlen =
				((Form_pg_type) GETSTRUCT(tup))->typlen;
			((Form_pg_attribute) to)->attstorage = 'p';
			((Form_pg_attribute) to)->attalign =
				((Form_pg_type) GETSTRUCT(tup))->typalign;
			((Form_pg_attribute) to)->atttypmod = IndexKeyType->typmod;
		}


		/* ----------------
		 *	  now we have to drop in the proper relation descriptor
		 *	  into the copied tuple form's attrelid and we should be
		 *	  all set.
		 * ----------------
		 */
		((Form_pg_attribute) to)->attrelid = heapoid;
	}

	return indexTupDesc;
}

/* ----------------------------------------------------------------
 * AccessMethodObjectIdGetForm
 *		Returns the formated access method tuple given its object identifier.
 *
 * XXX ADD INDEXING
 *
 * Note:
 *		Assumes object identifier is valid.
 * ----------------------------------------------------------------
 */
Form_pg_am
AccessMethodObjectIdGetForm(Oid accessMethodObjectId)
{
	Relation	pg_am_desc;
	HeapScanDesc pg_am_scan;
	HeapTuple	pg_am_tuple;
	ScanKeyData key;
	Form_pg_am	aform;

	/* ----------------
	 *	form a scan key for the pg_am relation
	 * ----------------
	 */
	ScanKeyEntryInitialize(&key, 0, ObjectIdAttributeNumber,
						   F_OIDEQ,
						   ObjectIdGetDatum(accessMethodObjectId));

	/* ----------------
	 *	fetch the desired access method tuple
	 * ----------------
	 */
	pg_am_desc = heap_openr(AccessMethodRelationName, AccessShareLock);
	pg_am_scan = heap_beginscan(pg_am_desc, SnapshotNow, 1, &key);

	pg_am_tuple = heap_getnext(pg_am_scan);

	/* ----------------
	 *	return NULL if not found
	 * ----------------
	 */
	if (!HeapTupleIsValid(pg_am_tuple))
	{
		heap_endscan(pg_am_scan);
		heap_close(pg_am_desc, AccessShareLock);
		return NULL;
	}

	/* ----------------
	 *	if found am tuple, then copy the form and return the copy
	 * ----------------
	 */
	aform = (Form_pg_am) palloc(sizeof *aform);
	memcpy(aform, GETSTRUCT(pg_am_tuple), sizeof *aform);

	heap_endscan(pg_am_scan);
	heap_close(pg_am_desc, AccessShareLock);

	return aform;
}

/* ----------------------------------------------------------------
 *		ConstructIndexReldesc
 * ----------------------------------------------------------------
 */
static void
ConstructIndexReldesc(Relation indexRelation, Oid amoid)
{
	MemoryContext oldcxt;

	/* ----------------
	 *	  here we make certain to allocate the access method
	 *	  tuple within the cache context lest it vanish when the
	 *	  context changes
	 * ----------------
	 */

	oldcxt = MemoryContextSwitchTo(RelationGetCacheContext());

	indexRelation->rd_am = AccessMethodObjectIdGetForm(amoid);

	MemoryContextSwitchTo(oldcxt);

	/* ----------------
	 *	 XXX missing the initialization of some other fields
	 * ----------------
	 */

	indexRelation->rd_rel->relowner = GetUserId();

	indexRelation->rd_rel->relam = amoid;
	indexRelation->rd_rel->reltuples = 1;		/* XXX */
	indexRelation->rd_rel->relkind = RELKIND_INDEX;
}

/* ----------------------------------------------------------------
 *		UpdateRelationRelation
 * ----------------------------------------------------------------
 */
static Oid
UpdateRelationRelation(Relation indexRelation, char *temp_relname)
{
	Relation	pg_class;
	HeapTuple	tuple;
	Oid			tupleOid;
	Relation	idescs[Num_pg_class_indices];

	pg_class = heap_openr(RelationRelationName, RowExclusiveLock);

	/* XXX Natts_pg_class_fixed is a hack - see pg_class.h */
	tuple = heap_addheader(Natts_pg_class_fixed,
						   sizeof(*indexRelation->rd_rel),
						   (char *) indexRelation->rd_rel);

	/* ----------------
	 *	the new tuple must have the same oid as the relcache entry for the
	 *	index.	sure would be embarassing to do this sort of thing in polite
	 *	company.
	 * ----------------
	 */
	tuple->t_data->t_oid = RelationGetRelid(indexRelation);
	heap_insert(pg_class, tuple);

	if (temp_relname)
		create_temp_relation(temp_relname, tuple);

	/*
	 * During normal processing, we need to make sure that the system
	 * catalog indices are correct.  Bootstrap (initdb) time doesn't
	 * require this, because we make sure that the indices are correct
	 * just before exiting.
	 */

	if (!IsIgnoringSystemIndexes())
	{
		CatalogOpenIndices(Num_pg_class_indices, Name_pg_class_indices, idescs);
		CatalogIndexInsert(idescs, Num_pg_class_indices, pg_class, tuple);
		CatalogCloseIndices(Num_pg_class_indices, idescs);
	}

	tupleOid = tuple->t_data->t_oid;
	heap_freetuple(tuple);
	heap_close(pg_class, RowExclusiveLock);

	return tupleOid;
}

/* ----------------------------------------------------------------
 *		InitializeAttributeOids
 * ----------------------------------------------------------------
 */
static void
InitializeAttributeOids(Relation indexRelation,
						int numatts,
						Oid indexoid)
{
	TupleDesc	tupleDescriptor;
	int			i;

	tupleDescriptor = RelationGetDescr(indexRelation);

	for (i = 0; i < numatts; i += 1)
		tupleDescriptor->attrs[i]->attrelid = indexoid;
}

/* ----------------------------------------------------------------
 *		AppendAttributeTuples
 *
 *		XXX For now, only change the ATTNUM attribute value
 * ----------------------------------------------------------------
 */
static void
AppendAttributeTuples(Relation indexRelation, int numatts)
{
	Relation	pg_attribute;
	HeapTuple	init_tuple,
				cur_tuple = NULL,
				new_tuple;
	bool		hasind;
	Relation	idescs[Num_pg_attr_indices];

	Datum		value[Natts_pg_attribute];
	char		nullv[Natts_pg_attribute];
	char		replace[Natts_pg_attribute];

	TupleDesc	indexTupDesc;
	int			i;

	/* ----------------
	 *	open the attribute relation
	 *	XXX ADD INDEXING
	 * ----------------
	 */
	pg_attribute = heap_openr(AttributeRelationName, RowExclusiveLock);

	/* ----------------
	 *	initialize *null, *replace and *value
	 * ----------------
	 */
	MemSet(nullv, ' ', Natts_pg_attribute);
	MemSet(replace, ' ', Natts_pg_attribute);

	/* ----------------
	 *	create the first attribute tuple.
	 *	XXX For now, only change the ATTNUM attribute value
	 * ----------------
	 */
	replace[Anum_pg_attribute_attnum - 1] = 'r';
	replace[Anum_pg_attribute_attcacheoff - 1] = 'r';

	value[Anum_pg_attribute_attnum - 1] = Int16GetDatum(1);
	value[Anum_pg_attribute_attcacheoff - 1] = Int32GetDatum(-1);

	init_tuple = heap_addheader(Natts_pg_attribute,
								ATTRIBUTE_TUPLE_SIZE,
							 (char *) (indexRelation->rd_att->attrs[0]));

	hasind = false;
	if (!IsIgnoringSystemIndexes() && pg_attribute->rd_rel->relhasindex)
	{
		hasind = true;
		CatalogOpenIndices(Num_pg_attr_indices, Name_pg_attr_indices, idescs);
	}

	/* ----------------
	 *	insert the first attribute tuple.
	 * ----------------
	 */
	cur_tuple = heap_modifytuple(init_tuple,
								 pg_attribute,
								 value,
								 nullv,
								 replace);
	heap_freetuple(init_tuple);

	heap_insert(pg_attribute, cur_tuple);
	if (hasind)
		CatalogIndexInsert(idescs, Num_pg_attr_indices, pg_attribute, cur_tuple);

	/* ----------------
	 *	now we use the information in the index cur_tuple
	 *	descriptor to form the remaining attribute tuples.
	 * ----------------
	 */
	indexTupDesc = RelationGetDescr(indexRelation);

	for (i = 1; i < numatts; i += 1)
	{
		/* ----------------
		 *	process the remaining attributes...
		 * ----------------
		 */
		memmove(GETSTRUCT(cur_tuple),
				(char *) indexTupDesc->attrs[i],
				ATTRIBUTE_TUPLE_SIZE);

		value[Anum_pg_attribute_attnum - 1] = Int16GetDatum(i + 1);

		new_tuple = heap_modifytuple(cur_tuple,
									 pg_attribute,
									 value,
									 nullv,
									 replace);
		heap_freetuple(cur_tuple);

		heap_insert(pg_attribute, new_tuple);
		if (hasind)
			CatalogIndexInsert(idescs, Num_pg_attr_indices, pg_attribute, new_tuple);

		/* ----------------
		 *	ModifyHeapTuple returns a new copy of a cur_tuple
		 *	so we free the original and use the copy..
		 * ----------------
		 */
		cur_tuple = new_tuple;
	}

	if (cur_tuple)
		heap_freetuple(cur_tuple);
	heap_close(pg_attribute, RowExclusiveLock);
	if (hasind)
		CatalogCloseIndices(Num_pg_attr_indices, idescs);

}

/* ----------------------------------------------------------------
 *		UpdateIndexRelation
 * ----------------------------------------------------------------
 */
static void
UpdateIndexRelation(Oid indexoid,
					Oid heapoid,
					FuncIndexInfo *funcInfo,
					int natts,
					AttrNumber *attNums,
					Oid *classOids,
					Node *predicate,
					List *attributeList,
                                        bool isdeferred,
					bool islossy,
					bool unique,
					bool primary)
{
	Form_pg_index indexForm;
	IndexElem  *IndexKey;
	char	   *predString;
	text	   *predText;
	int			predLen,
				itupLen;
	Relation	pg_index;
	HeapTuple	tuple;
	int			i;
	Relation	idescs[Num_pg_index_indices];
        char attributes  = 0;
        
        if ( islossy ) attributes |= INDEX_LOSSY;
        if ( isdeferred ) attributes |= INDEX_DEFERRED;

	/* ----------------
	 *	allocate an Form_pg_index big enough to hold the
	 *	index-predicate (if any) in string form
	 * ----------------
	 */
	if (predicate != NULL)
	{
		predString = nodeToString(predicate);
		predText = (text *) fmgr(F_TEXTIN, predString);
		pfree(predString);
	}
	else
		predText = (text *) fmgr(F_TEXTIN, "");

	predLen = VARSIZE(predText);
	itupLen = predLen + sizeof(FormData_pg_index);
	indexForm = (Form_pg_index) palloc(itupLen);
	memset(indexForm, 0, sizeof(FormData_pg_index));

	memmove((char *) &indexForm->indpred, (char *) predText, predLen);

	/* ----------------
	 *	store the oid information into the index tuple form
	 * ----------------
	 */
	indexForm->indrelid = heapoid;
	indexForm->indexrelid = indexoid;
	indexForm->indproc = (PointerIsValid(funcInfo)) ?
		FIgetProcOid(funcInfo) : InvalidOid;
	indexForm->indattributes = attributes;
	indexForm->indisprimary = primary;
	indexForm->indisunique = unique;

	indexForm->indhaskeytype = 0;
	while (attributeList != NIL)
	{
		IndexKey = (IndexElem *) lfirst(attributeList);
		if (IndexKey->typename != NULL)
		{
			indexForm->indhaskeytype = 1;
			break;
		}
		attributeList = lnext(attributeList);
	}

	MemSet((char *) &indexForm->indkey[0], 0, sizeof indexForm->indkey);
	MemSet((char *) &indexForm->indclass[0], 0, sizeof indexForm->indclass);

	/* ----------------
	 *	copy index key and op class information
	 * ----------------
	 */
	for (i = 0; i < natts; i += 1)
	{
		indexForm->indkey[i] = attNums[i];
		indexForm->indclass[i] = classOids[i];
	}

	/*
	 * If we have a functional index, add all attribute arguments
	 */
	if (PointerIsValid(funcInfo))
	{
		for (i = 1; i < FIgetnArgs(funcInfo); i++)
			indexForm->indkey[i] = attNums[i];
	}

	indexForm->indisclustered = '\0';	/* XXX constant */

	/* ----------------
	 *	open the system catalog index relation
	 * ----------------
	 */
	pg_index = heap_openr(IndexRelationName, RowExclusiveLock);

	/* ----------------
	 *	form a tuple to insert into pg_index
	 * ----------------
	 */
	tuple = heap_addheader(Natts_pg_index,
						   itupLen,
						   (char *) indexForm);

	/* ----------------
	 *	insert the tuple into the pg_index
	 *	XXX ADD INDEX TUPLES TOO
	 * ----------------
	 */
	heap_insert(pg_index, tuple);

	/* ----------------
	 *	insert the index tuple into the pg_index
	 * ----------------
	 */
	if (!IsIgnoringSystemIndexes())
	{
		CatalogOpenIndices(Num_pg_index_indices, Name_pg_index_indices, idescs);
		CatalogIndexInsert(idescs, Num_pg_index_indices, pg_index, tuple);
		CatalogCloseIndices(Num_pg_index_indices, idescs);
	}
	/* ----------------
	 *	close the relation and free the tuple
	 * ----------------
	 */
	heap_close(pg_index, RowExclusiveLock);
	pfree(predText);
	pfree(indexForm);
	heap_freetuple(tuple);
}

/* ----------------------------------------------------------------
 *		UpdateIndexPredicate
 * ----------------------------------------------------------------
 */
void
UpdateIndexPredicate(Oid indexoid, Node *oldPred, Node *predicate)
{
	Node	   *newPred;
	char	   *predString;
	text	   *predText;
	Relation	pg_index;
	HeapTuple	tuple;
	HeapTuple	newtup;
	int			i;
	Datum		values[Natts_pg_index];
	char		nulls[Natts_pg_index];
	char		replace[Natts_pg_index];

	/*
	 * Construct newPred as a CNF expression equivalent to the OR of the
	 * original partial-index predicate ("oldPred") and the extension
	 * predicate ("predicate").
	 *
	 * This should really try to process the result to change things like
	 * "a>2 OR a>1" to simply "a>1", but for now all it does is make sure
	 * that if the extension predicate is NULL (i.e., it is being extended
	 * to be a complete index), then newPred will be NULL - in effect,
	 * changing "a>2 OR TRUE" to "TRUE". --Nels, Jan '93
	 */
	newPred = NULL;
	if (predicate != NULL)
	{
		newPred = (Node *) make_orclause(lcons(make_andclause((List *) predicate),
								  lcons(make_andclause((List *) oldPred),
										NIL)));
		newPred = (Node *) cnfify((Expr *) newPred, true);
	}

	/* translate the index-predicate to string form */
	if (newPred != NULL)
	{
		predString = nodeToString(newPred);
		predText = (text *) fmgr(F_TEXTIN, predString);
		pfree(predString);
	}
	else
		predText = (text *) fmgr(F_TEXTIN, "");

	/* open the index system catalog relation */
	pg_index = heap_openr(IndexRelationName, RowExclusiveLock);

	tuple = SearchSysCacheTuple(INDEXRELID,
								ObjectIdGetDatum(indexoid),
								0, 0, 0);
	Assert(HeapTupleIsValid(tuple));

	for (i = 0; i < Natts_pg_index; i++)
	{
		nulls[i] = heap_attisnull(tuple, i + 1) ? 'n' : ' ';
		replace[i] = ' ';
		values[i] = (Datum) NULL;
	}

	replace[Anum_pg_index_indpred - 1] = 'r';
	values[Anum_pg_index_indpred - 1] = (Datum) predText;

	newtup = heap_modifytuple(tuple, pg_index, values, nulls, replace);

	heap_update(pg_index, &newtup->t_self, newtup, NULL, NULL);

	heap_freetuple(newtup);
	heap_close(pg_index, RowExclusiveLock);
	pfree(predText);
}

/* ----------------------------------------------------------------
 *		InitIndexStrategy
 * ----------------------------------------------------------------
 */
void
InitIndexStrategy(int numatts,
				  Relation indexRelation,
				  Oid accessMethodObjectId)
{
	IndexStrategy strategy;
	RegProcedure *support;
	uint16		amstrategies;
	uint16		amsupport;
	Oid			attrelid;
	Size		strsize;

	/* ----------------
	 *	get information from the index relation descriptor
	 * ----------------
	 */
	attrelid = indexRelation->rd_att->attrs[0]->attrelid;
	amstrategies = indexRelation->rd_am->amstrategies;
	amsupport = indexRelation->rd_am->amsupport;

	/* ----------------
	 *	get the size of the strategy
	 * ----------------
	 */
	strsize = AttributeNumberGetIndexStrategySize(numatts, amstrategies);

	strategy = (IndexStrategy)
		MemoryContextAlloc(RelationGetCacheContext(), strsize);

	if (amsupport > 0)
	{
		strsize = numatts * (amsupport * sizeof(RegProcedure));
		support = (RegProcedure *) MemoryContextAlloc(RelationGetCacheContext(),
													  strsize);
	}
	else
		support = (RegProcedure *) NULL;

	/* ----------------
	 *	fill in the index strategy structure with information
	 *	from the catalogs.	First we must advance the command counter
	 *	so that we will see the newly-entered index catalog tuples.
	 * ----------------
	 */
	CommandCounterIncrement();

	IndexSupportInitialize(strategy, support,
						   attrelid, accessMethodObjectId,
						   amstrategies, amsupport, numatts);

	/* ----------------
	 *	store the strategy information in the index reldesc
	 * ----------------
	 */
	RelationSetIndexSupport(indexRelation, strategy, support);
}


/* ----------------------------------------------------------------
 *		index_create
 * ----------------------------------------------------------------
 */
void
index_create(char *heapRelationName,
			 char *indexRelationName,
			 FuncIndexInfo *funcInfo,
			 List *attributeList,
			 Oid accessMethodObjectId,
			 int numatts,
			 AttrNumber *attNums,
			 Oid *classObjectId,
			 uint16 parameterCount,
			 Datum *parameter,
			 Node *predicate,
			 bool isdeferred,
			 bool islossy,
			 bool unique,
			 bool primary)
{
	Relation	heapRelation;
	Relation	indexRelation;
	TupleDesc	indexTupDesc;
	Oid			heapoid;
	Oid			indexoid;
	PredInfo   *predInfo;
	bool		istemp = (get_temp_rel_by_username(heapRelationName) != NULL);
	char	   *temp_relname = NULL;
	CommitType		savetype = GetTransactionCommitType();

	/* ----------------
	 *	check parameters
	 * ----------------
	 */
	SetReindexProcessing(false);
	if (numatts < 1)
		elog(ERROR, "must index at least one attribute");

	/* ----------------
	 *	  get heap relation oid and open the heap relation
	 *	  XXX ADD INDEXING
	 * ----------------
	 */
	heapoid = GetHeapRelationOid(heapRelationName, indexRelationName, istemp);

	/*
	 * Only SELECT ... FOR UPDATE are allowed while doing this
	 */
	heapRelation = heap_open(heapoid, ShareLock);

	/* ----------------
	 *	  construct new tuple descriptor
	 * ----------------
	 */
	if (PointerIsValid(funcInfo))
		indexTupDesc = BuildFuncTupleDesc(funcInfo);
	else
		indexTupDesc = ConstructTupleDescriptor(heapoid,
												heapRelation,
												attributeList,
												numatts,
												attNums);

	/* save user relation name because heap_create changes it */
	if (istemp)
	{
		temp_relname = pstrdup(indexRelationName);		/* save original value */
		indexRelationName = palloc(NAMEDATALEN);
		strcpy(indexRelationName, temp_relname);		/* heap_create will
														 * change this */
	}

	/* ----------------
	 *	create the index relation
	 * ----------------
	 */
	indexRelation = heap_create(indexRelationName,
								indexTupDesc, false, istemp, false);

	/* ----------------
	 *	  construct the index relation descriptor
	 *
	 *	  XXX should have a proper way to create cataloged relations
	 * ----------------
	 */
	ConstructIndexReldesc(indexRelation, accessMethodObjectId);

	/* ----------------
	 *	  add index to catalogs
	 *	  (append RELATION tuple)
	 * ----------------
	 */
	indexoid = UpdateRelationRelation(indexRelation, temp_relname);

	/*
	 * We create the disk file for this relation here
	 */
	heap_storage_create(indexRelation);

	/* ----------------
	 *	now update the object id's of all the attribute
	 *	tuple forms in the index relation's tuple descriptor
	 * ----------------
	 */
	InitializeAttributeOids(indexRelation, numatts, indexoid);

	/* ----------------
	 *	  append ATTRIBUTE tuples
	 * ----------------
	 */
	AppendAttributeTuples(indexRelation, numatts);

	/* ----------------
	 *	  update pg_index
	 *	  (append INDEX tuple)
	 *
	 *	  Note that this stows away a representation of "predicate".
	 *	  (Or, could define a rule to maintain the predicate) --Nels, Feb '92
	 * ----------------
	 */
	UpdateIndexRelation(indexoid, heapoid, funcInfo,
						numatts, attNums, classObjectId, predicate,
						attributeList, isdeferred, islossy, unique, primary);

	predInfo = (PredInfo *) palloc(sizeof(PredInfo));
	predInfo->pred = predicate;
	predInfo->oldPred = NULL;

	/* ----------------
	 *	  initialize the index strategy
	 * ----------------
	 */
	InitIndexStrategy(numatts, indexRelation, accessMethodObjectId);

	/*
	 * If this is bootstrap (initdb) time, then we don't actually fill in
	 * the index yet.  We'll be creating more indices and classes later,
	 * so we delay filling them in until just before we're done with
	 * bootstrapping.  Otherwise, we call the routine that constructs the
	 * index.
	 *
	 * In normal processing mode, the heap and index relations are closed by
	 * index_build() --- but we continue to hold the ShareLock on the heap
	 * that we acquired above, until end of transaction.
         *
	 */
	if (IsBootstrapProcessingMode())
	{
		index_register(heapRelationName, indexRelationName, numatts, attNums,
					   parameterCount, parameter, funcInfo, predInfo);
        	heap_close(heapRelation, NoLock);
        	index_close(indexRelation);
	}
	else
	{
		index_build(heapRelation, indexRelation, numatts, attNums,
					parameterCount, parameter, funcInfo, predInfo);
	}
              
}

/* ----------------------------------------------------------------
 *
 *		index_drop
 *
 * ----------------------------------------------------------------
 */
void
index_drop(Oid indexId)
{
	Relation	userHeapRelation;
	Relation	userIndexRelation;
	Relation	indexRelation;
	Relation	relationRelation;
	Relation	attributeRelation;
	HeapTuple	tuple;
	int16		attnum;
	CommitType		savetype = GetTransactionCommitType();

	Assert(OidIsValid(indexId));

	/* ----------------
	 *	To drop an index safely, we must grab exclusive lock on its parent
	 *	table; otherwise there could be other backends using the index!
	 *	Exclusive lock on the index alone is insufficient because the index
	 *	access routines are a little slipshod about obtaining adequate locking
	 *	(see ExecOpenIndices()).  We do grab exclusive lock on the index too,
	 *	just to be safe.  Both locks must be held till end of transaction,
	 *	else other backends will still see this index in pg_index.
	 * ----------------
	 */
	userHeapRelation = heap_open(IndexGetRelation(indexId),
								 AccessExclusiveLock);

	userIndexRelation = index_open(indexId);
	LockRelation(userIndexRelation, AccessExclusiveLock);

	/* ----------------
	 *	DROP INDEX within a transaction block is dangerous, because
	 *	if the transaction is later rolled back there will be no way to
	 *	undo the unlink of the relation's physical file.  For now, allow it
	 *	but emit a warning message.
	 *	Someday we might want to consider postponing the physical unlink
	 *	until transaction commit, but that's a lot of work...
	 *	The only case that actually works right is for relations created
	 *	in the current transaction, since the post-abort state would be that
	 *	they don't exist anyway.  So, no warning in that case.
	 * ----------------
	 */
	if (IsTransactionBlock() && !userIndexRelation->rd_myxactonly)
		elog(NOTICE, "Caution: DROP INDEX cannot be rolled back, so don't abort now");

	/* ----------------
	 * fix DESCRIPTION relation
	 * ----------------
	 */
	DeleteComments(indexId);

	/* ----------------
	 * fix RELATION relation
	 * ----------------
	 */
	relationRelation = heap_openr(RelationRelationName, RowExclusiveLock);

	tuple = SearchSysCacheTupleCopy(RELOID,
									ObjectIdGetDatum(indexId),
									0, 0, 0);

	Assert(HeapTupleIsValid(tuple));

	heap_delete(relationRelation, &tuple->t_self, NULL,NULL);
	heap_freetuple(tuple);
	heap_close(relationRelation, RowExclusiveLock);

	/* ----------------
	 * fix ATTRIBUTE relation
	 * ----------------
	 */
	attributeRelation = heap_openr(AttributeRelationName, RowExclusiveLock);

	attnum = 1;					/* indexes start at 1 */

	while (HeapTupleIsValid(tuple = SearchSysCacheTupleCopy(ATTNUM,
											   ObjectIdGetDatum(indexId),
												   Int16GetDatum(attnum),
															0, 0)))
	{
		heap_delete(attributeRelation, &tuple->t_self, NULL,NULL);
		heap_freetuple(tuple);
		attnum++;
	}
	heap_close(attributeRelation, RowExclusiveLock);

	/* ----------------
	 * fix INDEX relation
	 * ----------------
	 */
	indexRelation = heap_openr(IndexRelationName, RowExclusiveLock);

	tuple = SearchSysCacheTupleCopy(INDEXRELID,
									ObjectIdGetDatum(indexId),
									0, 0, 0);
	Assert(HeapTupleIsValid(tuple));

	heap_delete(indexRelation, &tuple->t_self, NULL,NULL);
	heap_freetuple(tuple);
	heap_close(indexRelation, RowExclusiveLock);

	/*
	 * flush buffer cache and physically remove the file
	 */
	InvalidateRelationBuffers(userIndexRelation);
        DropVacuumRequests(indexId,GetDatabaseId());        
        ForgetFreespace(userIndexRelation,true);
	
        if (smgrunlink(userIndexRelation->rd_smgr) != SM_SUCCESS) {
              elog(ERROR, "index_drop: unlink: %m");
        } else {
              userIndexRelation->rd_smgr = NULL;
        }
	/*
	 * Close rels, but keep locks
	 */
	index_close(userIndexRelation);
        ImmediateSharedRelationCacheInvalidate(userIndexRelation);
	RelationForgetRelation(indexId,GetDatabaseId());

	heap_close(userHeapRelation, NoLock);

	/* does something only if it is a temp index */
	remove_temp_relation(indexId);
}

/* ----------------------------------------------------------------
 *						index_build support
 * ----------------------------------------------------------------
 */
/* ----------------
 *		FormIndexDatum
 * ----------------
 */

void
FormIndexDatum(int numberOfAttributes,
			   AttrNumber *attributeNumber,
			   HeapTuple heapTuple,
			   TupleDesc heapDescriptor,
			   Datum *datum,
			   char *nullv,
			   FuncIndexInfoPtr fInfo)
{
	AttrNumber	i;
	int			offset;
	bool		isNull;

	/* ----------------
	 *	for each attribute we need from the heap tuple,
	 *	get the attribute and stick it into the datum and
	 *	null arrays.
	 * ----------------
	 */

	for (i = 1; i <= numberOfAttributes; i++)
	{
		offset = AttrNumberGetAttrOffset(i);

		datum[offset] = PointerGetDatum(GetIndexValue(heapTuple,
													  heapDescriptor,
													  offset,
													  attributeNumber,
													  fInfo,
													  &isNull));

		nullv[offset] = (isNull) ? 'n' : ' ';
	}
}

/* --------------------------------------------
 *		Lock class info for update
 * --------------------------------------------
 */
static
bool
LockClassinfoForUpdate(Oid relid, HeapTuple rtup, bool confirmCommitted)
{
	HeapTuple	classTuple;
//	Form_pg_class pgcform;
	bool		test;
	Relation	relationRelation;
        Buffer          buffer;

	classTuple = SearchSysCacheTuple(RELOID, PointerGetDatum(relid),
									 0, 0, 0);
	if (!HeapTupleIsValid(classTuple))
		return false;
	rtup->t_self = classTuple->t_self;
//	pgcform = (Form_pg_class) GETSTRUCT(classTuple);

	relationRelation = heap_openr(RelationRelationName, RowShareLock);

	test = heap_mark4update(relationRelation, &buffer, rtup,  NULL);
        ReleaseBuffer(relationRelation,buffer);

	switch (test)
	{
		case HeapTupleSelfUpdated:
		case HeapTupleMayBeUpdated:
			break;
		default:
			elog(ERROR, "LockStatsForUpdate couldn't lock relid %lu", relid);
			return false;
	}
	RelationInvalidateHeapTuple(relationRelation, rtup);
	if (confirmCommitted)
	{
		HeapTupleHeader th = rtup->t_data;

		if (!(th->t_infomask & HEAP_XMIN_COMMITTED))
			elog(ERROR, "The tuple isn't committed");
		if (th->t_infomask & HEAP_XMAX_COMMITTED)
			if (!(th->t_infomask & HEAP_MARKED_FOR_UPDATE))
				elog(ERROR, "The tuple is already deleted");
	}
	heap_close(relationRelation, NoLock);
	return true;
}

/* ---------------------------------------------
 *		Indexes of the relation active ?
 * ---------------------------------------------
 */
bool
IndexesAreActive(Oid relid, bool confirmCommitted)
{
	HeapTupleData tuple;
	Relation	indexRelation;
	HeapScanDesc scan;
	ScanKeyData entry;
	bool		isactive;

	if (!LockClassinfoForUpdate(relid, &tuple, confirmCommitted))
		elog(ERROR, "IndexesAreActive couldn't lock %lu", relid);
	if (((Form_pg_class) GETSTRUCT(&tuple))->relkind != RELKIND_RELATION)
		elog(ERROR, "relation %lu isn't an relation", relid);
	isactive = ((Form_pg_class) GETSTRUCT(&tuple))->relhasindex;

	if (isactive)
		return isactive;
        
	indexRelation = heap_openr(IndexRelationName, AccessShareLock);

	ScanKeyEntryInitialize(&entry, 0, Anum_pg_index_indrelid,
						   F_OIDEQ, ObjectIdGetDatum(relid));
	scan = heap_beginscan(indexRelation, SnapshotNow,
						  1, &entry);
	if (!heap_getnext(scan))
		isactive = true;
	heap_endscan(scan);
	heap_close(indexRelation, NoLock);
	return isactive;
}

/* ----------------
 *		set relhasindex of pg_class in place
 * ----------------
 */
void
setRelhasindexInplace(Oid relid, bool hasindex, bool immediate)
{
	Relation	whichRel;
	Relation	pg_class;
	HeapTuple	tuple;
	Form_pg_class rd_rel;
	HeapScanDesc pg_class_scan = NULL;

	/* ----------------
	 * This routine handles updates for only the heap relation
	 * hasindex. In order to guarantee that we're able to *see* the index
	 * relation tuple, we bump the command counter id here.
	 * ----------------
	 */
	CommandCounterIncrement();

	/* ----------------
	 * CommandCounterIncrement() flushes invalid cache entries, including
	 * those for the heap and index relations for which we're updating
	 * statistics.	Now that the cache is flushed, it's safe to open the
	 * relation again.	We need the relation open in order to figure out
	 * how many blocks it contains.
	 * ----------------
	 */

	whichRel = heap_open(relid, ShareLock);

	if (!RelationIsValid(whichRel))
		elog(ERROR, "setRelhasindexInplace: cannot open relation id %lu", relid);

	/* ----------------
	 * Find the RELATION relation tuple for the given relation.
	 * ----------------
	 */
	pg_class = heap_openr(RelationRelationName, RowExclusiveLock);
	if (!RelationIsValid(pg_class))
		elog(ERROR, "setRelhasindexInplace: could not open RELATION relation");

	if (!IsIgnoringSystemIndexes())
	{
		tuple = SearchSysCacheTupleCopy(RELOID,
										ObjectIdGetDatum(relid), 0, 0, 0);
	}
	else
	{
		ScanKeyData key[1];

		ScanKeyEntryInitialize(&key[0], 0,
							   ObjectIdAttributeNumber,
							   F_OIDEQ,
							   ObjectIdGetDatum(relid));

		pg_class_scan = heap_beginscan(pg_class, SnapshotNow, 1, key);
		tuple = heap_getnext(pg_class_scan);
	}

	if (!HeapTupleIsValid(tuple))
	{
		if (pg_class_scan)
			heap_endscan(pg_class_scan);
		heap_close(pg_class, RowExclusiveLock);
		elog(ERROR, "setRelhasindexInplace: cannot scan RELATION relation");
	}

	/*
	 * Confirm that target tuple is locked by this transaction in case of
	 * immedaite updation.
	 */
	if (immediate)
	{
		HeapTupleHeader th = tuple->t_data;

		if (!(th->t_infomask & HEAP_XMIN_COMMITTED))
			elog(ERROR, "Immediate hasindex updation can be done only for committed tuples %x", th->t_infomask);
		if (th->t_infomask & HEAP_XMAX_INVALID)
			elog(ERROR, "Immediate hasindex updation can be done only for locked tuples %x", th->t_infomask);
		if (th->t_infomask & HEAP_XMAX_COMMITTED)
			elog(ERROR, "Immediate hasindex updation can be done only for locked tuples %x", th->t_infomask);
		if (!(th->t_infomask & HEAP_MARKED_FOR_UPDATE))
			elog(ERROR, "Immediate hasindex updation can be done only for locked tuples %x", th->t_infomask);
		if (!(TransactionIdIsCurrentTransactionId(th->t_xmax)))
			elog(ERROR, "The updating tuple is already locked by another backend");
	}

	/*
	 * We shouldn't have to do this, but we do...  Modify the reldesc in
	 * place with the new values so that the cache contains the latest
	 * copy.
	 */
	whichRel->rd_rel->relhasindex = hasindex;

	/* ----------------
	 *	Update hasindex in pg_class.
	 * ----------------
	 */
	if (pg_class_scan)
	{

		if (!IsBootstrapProcessingMode())
			ImmediateInvalidateSharedHeapTuple(pg_class, tuple);
		rd_rel = (Form_pg_class) GETSTRUCT(tuple);
		rd_rel->relhasindex = hasindex;
		WriteNoReleaseBuffer(pg_class_scan->rs_rd,pg_class_scan->rs_cbuf);
	}
	else
	{
		HeapTupleData htup;
		Buffer		buffer;

		htup.t_self = tuple->t_self;
		heap_fetch(pg_class, SnapshotNow, &htup, &buffer);
		ImmediateInvalidateSharedHeapTuple(pg_class, tuple);
		rd_rel = (Form_pg_class) GETSTRUCT(&htup);
		rd_rel->relhasindex = hasindex;
		WriteBuffer(pg_class, buffer);
	}

	if (!pg_class_scan)
		heap_freetuple(tuple);
	else
		heap_endscan(pg_class_scan);

	heap_close(pg_class, NoLock);
	heap_close(whichRel, NoLock);
}

/* ----------------
 *		UpdateStats
 * ----------------
 */
void
UpdateStats(Oid relid, long reltuples)
{
	Relation	whichRel;
	Relation	pg_class;
	HeapTuple	tuple;
	HeapTuple	newtup;
	long		relpages;
	int			i;
	Form_pg_class rd_rel;
	Relation	idescs[Num_pg_class_indices];
	Datum		values[Natts_pg_class];
	char		nulls[Natts_pg_class];
	char		replace[Natts_pg_class];
	HeapScanDesc pg_class_scan = NULL;
	bool		in_place_upd;
        bool		inplace = IsReindexProcessing();

	/* ----------------
	 * This routine handles updates for both the heap and index relation
	 * statistics.	In order to guarantee that we're able to *see* the index
	 * relation tuple, we bump the command counter id here.  The index
	 * relation tuple was created in the current transaction.
	 * ----------------
	 */
	CommandCounterIncrement();

	/* ----------------
	 * CommandCounterIncrement() flushes invalid cache entries, including
	 * those for the heap and index relations for which we're updating
	 * statistics.	Now that the cache is flushed, it's safe to open the
	 * relation again.	We need the relation open in order to figure out
	 * how many blocks it contains.
	 * ----------------
	 */

	/*
	 * Can't use heap_open here since we don't know if it's an index...
	 */
	whichRel = RelationIdGetRelation(relid,DEFAULTDBOID);

	if (!RelationIsValid(whichRel))
		elog(ERROR, "UpdateStats: cannot open relation id %lu", relid);

	LockRelation(whichRel, ShareLock);

	/* ----------------
	 * Find the RELATION relation tuple for the given relation.
	 * ----------------
	 */
	pg_class = heap_openr(RelationRelationName, RowExclusiveLock);
	if (!RelationIsValid(pg_class))
		elog(ERROR, "UpdateStats: could not open RELATION relation");

	in_place_upd = (inplace || IsBootstrapProcessingMode());
	if (!in_place_upd)
	{
		tuple = SearchSysCacheTupleCopy(RELOID,
										ObjectIdGetDatum(relid),
										0, 0, 0);
	}
	else
	{
		ScanKeyData key[1];

		ScanKeyEntryInitialize(&key[0], 0,
							   ObjectIdAttributeNumber,
							   F_OIDEQ,
							   ObjectIdGetDatum(relid));

		pg_class_scan = heap_beginscan(pg_class, SnapshotNow, 1, key);
		tuple = heap_getnext(pg_class_scan);
	}

	if (!HeapTupleIsValid(tuple))
	{
		if (pg_class_scan)
			heap_endscan(pg_class_scan);
		heap_close(pg_class, RowExclusiveLock);
		elog(ERROR, "UpdateStats: cannot scan RELATION relation");
	}

	/* ----------------
	 * Figure values to insert.
	 *
	 * If we found zero tuples in the scan, do NOT believe it; instead put
	 * a bogus estimate into the statistics fields.  Otherwise, the common
	 * pattern "CREATE TABLE; CREATE INDEX; insert data" leaves the table
	 * with zero size statistics until a VACUUM is done.  The optimizer will
	 * generate very bad plans if the stats claim the table is empty when
	 * it is actually sizable.	See also CREATE TABLE in heap.c.
	 * ----------------
	 */
	relpages = RelationGetNumberOfBlocks(whichRel);

	if (reltuples == 0)
	{
		if (relpages == 0)
		{
			/* Bogus defaults for a virgin table, same as heap.c */
			reltuples = 1000;
			relpages = 10;
		}
		else if (whichRel->rd_rel->relkind == RELKIND_INDEX && relpages <= 2)
		{
			/* Empty index, leave bogus defaults in place */
			reltuples = 1000;
		}
		else
			reltuples = relpages * NTUPLES_PER_PAGE(whichRel->rd_rel->relnatts);
	}

	/*
	 * We shouldn't have to do this, but we do...  Modify the reldesc in
	 * place with the new values so that the cache contains the latest
	 * copy.
	 */
	whichRel->rd_rel->relpages = relpages;
	whichRel->rd_rel->reltuples = reltuples;

	/* ----------------
	 *	Update statistics in pg_class.
	 * ----------------
	 */
	if (in_place_upd)
	{

		/*
		 * At bootstrap time, we don't need to worry about concurrency or
		 * visibility of changes, so we cheat.
		 */
		if (!IsBootstrapProcessingMode())
			ImmediateInvalidateSharedHeapTuple(pg_class, tuple);
		rd_rel = (Form_pg_class) GETSTRUCT(tuple);
		rd_rel->relpages = relpages;
		rd_rel->reltuples = reltuples;
		WriteNoReleaseBuffer(pg_class_scan->rs_rd, pg_class_scan->rs_cbuf);
	}
	else
	{
		/* During normal processing, must work harder. */

		for (i = 0; i < Natts_pg_class; i++)
		{
			nulls[i] = heap_attisnull(tuple, i + 1) ? 'n' : ' ';
			replace[i] = ' ';
			values[i] = (Datum) NULL;
		}

		replace[Anum_pg_class_relpages - 1] = 'r';
		values[Anum_pg_class_relpages - 1] = LongGetDatum(relpages);
		replace[Anum_pg_class_reltuples - 1] = 'r';
		values[Anum_pg_class_reltuples - 1] = LongGetDatum(reltuples);
		newtup = heap_modifytuple(tuple, pg_class, values, nulls, replace);
		heap_update(pg_class, &tuple->t_self, newtup, NULL, NULL);
		if (!IsIgnoringSystemIndexes())
		{
			CatalogOpenIndices(Num_pg_class_indices, Name_pg_class_indices, idescs);
			CatalogIndexInsert(idescs, Num_pg_class_indices, pg_class, newtup);
			CatalogCloseIndices(Num_pg_class_indices, idescs);
		}
		heap_freetuple(newtup);
	}

	if (!pg_class_scan)
		heap_freetuple(tuple);
	else
		heap_endscan(pg_class_scan);

	heap_close(pg_class, RowExclusiveLock);
	/* Cheating a little bit since we didn't open it with heap_open... */
	heap_close(whichRel, ShareLock);
}


/* -------------------------
 *		FillDummyExprContext
 *			Sets up dummy ExprContext and TupleTableSlot objects for use
 *			with ExecQual.
 *
 *			NOTE: buffer is passed for historical reasons; it should
 *			almost certainly always be InvalidBuffer.
 * -------------------------
 */
void
FillDummyExprContext(ExprContext *econtext,
					 TupleTableSlot *slot,
					 TupleDesc tupdesc)
{
	econtext->ecxt_scantuple = slot;
	econtext->ecxt_innertuple = NULL;
	econtext->ecxt_outertuple = NULL;
	econtext->ecxt_param_list_info = NULL;
	econtext->ecxt_range_table = NULL;

	ExecSetSlotDescriptor(slot,tupdesc);
}


/* ----------------
 *		DefaultBuild
 * ----------------
 */
static void
DefaultBuild(Relation heapRelation,
			 Relation indexRelation,
			 int numberOfAttributes,
			 AttrNumber *attributeNumber,
			 IndexStrategy indexStrategy,		/* not used */
			 uint16 parameterCount,		/* not used */
			 Datum *parameter,	/* not used */
			 FuncIndexInfoPtr funcInfo,
			 PredInfo *predInfo)
{
	HeapScanDesc scan;
	HeapTuple	heapTuple;
	IndexTuple	indexTuple;
	TupleDesc	heapDescriptor;
	TupleDesc	indexDescriptor;
	Datum	   *datum;
	char	   *nullv;
	long		reltuples,
				indtuples;
				

#ifndef OMIT_PARTIAL_INDEX
	ExprContext *econtext;
	TupleTable	tupleTable;
	TupleTableSlot *slot;

#endif
	Node	   *predicate;
	Node	   *oldPred;

	InsertIndexResult insertResult;

	/* ----------------
	 *	more & better checking is needed
	 * ----------------
	 */
	Assert(OidIsValid(indexRelation->rd_rel->relam));	/* XXX */
	

	/* ----------------
	 *	get the tuple descriptors from the relations so we know
	 *	how to form the index tuples..
	 * ----------------
	 */
	heapDescriptor = RelationGetDescr(heapRelation);
	indexDescriptor = RelationGetDescr(indexRelation);

	/* ----------------
	 *	datum and null are arrays in which we collect the index attributes
	 *	when forming a new index tuple.
	 * ----------------
	 */
	datum = (Datum *) palloc(numberOfAttributes * sizeof *datum);
	nullv = (char *) palloc(numberOfAttributes * sizeof *nullv);

	/*
	 * If this is a predicate (partial) index, we will need to evaluate
	 * the predicate using ExecQual, which requires the current tuple to
	 * be in a slot of a TupleTable.  In addition, ExecQual must have an
	 * ExprContext referring to that slot.	Here, we initialize dummy
	 * TupleTable and ExprContext objects for this purpose. --Nels, Feb
	 * '92
	 */

	predicate = predInfo->pred;
	oldPred = predInfo->oldPred;

#ifndef OMIT_PARTIAL_INDEX
	if (predicate != NULL || oldPred != NULL)
	{
		tupleTable = ExecCreateTupleTable(1);
		slot = ExecAllocTableSlot(tupleTable);
		econtext = makeNode(ExprContext);
		FillDummyExprContext(econtext, slot, heapDescriptor);
	}
	else
	{
		econtext = NULL;
		tupleTable = 0;
		slot = NULL;
	}
#endif	 /* OMIT_PARTIAL_INDEX */

	/* ----------------
	 *	Ok, begin our scan of the base relation.
	 * ----------------
	 */
	scan = heap_beginscan(heapRelation, /* relation */
						  SnapshotNow,	/* seeself */
						  0,	/* number of keys */
						  (ScanKey) NULL);		/* scan key */

	reltuples = indtuples = 0;

	/* ----------------
	 *	for each tuple in the base relation, we create an index
	 *	tuple and add it to the index relation.  We keep a running
	 *	count of the number of tuples so that we can update pg_class
	 *	with correct statistics when we're done building the index.
	 * ----------------
	 */
	while (HeapTupleIsValid(heapTuple = heap_getnext(scan)))
	{
		reltuples++;

#ifndef OMIT_PARTIAL_INDEX

		/*
		 * If oldPred != NULL, this is an EXTEND INDEX command, so skip
		 * this tuple if it was already in the existing partial index
		 */
		if (oldPred != NULL)
		{
			/* SetSlotContents(slot, heapTuple); */
			ExecStoreTuple(heapTuple,slot,false);
			if (ExecQual((List *) oldPred, econtext, false))
			{
				indtuples++;
				continue;
			}
		}

		/*
		 * Skip this tuple if it doesn't satisfy the partial-index
		 * predicate
		 */
		if (predicate != NULL)
		{
			/* SetSlotContents(slot, heapTuple); */
			ExecStoreTuple(heapTuple,slot,false);
			if (!ExecQual((List *) predicate, econtext, false))
				continue;
		}
#endif	 /* OMIT_PARTIAL_INDEX */

		indtuples++;

		/* ----------------
		 *	FormIndexDatum fills in its datum and null parameters
		 *	with attribute information taken from the given heap tuple.
		 * ----------------
		 */
		FormIndexDatum(numberOfAttributes,		/* num attributes */
					   attributeNumber, /* array of att nums to extract */
					   heapTuple,		/* tuple from base relation */
					   heapDescriptor,	/* heap tuple's descriptor */
					   datum,	/* return: array of attributes */
					   nullv,	/* return: array of char's */
					   funcInfo);

		indexTuple = index_formtuple(indexDescriptor,
									 datum,
									 nullv);
		indexTuple->t_tid = heapTuple->t_self;

		insertResult = index_insert(indexRelation, datum, nullv,
									&(heapTuple->t_self), heapRelation,false);

		if (insertResult)
			pfree(insertResult);
		pfree(indexTuple);
	}

	heap_endscan(scan);

#ifndef OMIT_PARTIAL_INDEX
	if (predicate != NULL || oldPred != NULL)
	{
		/* parameter was 'false', almost certainly wrong --- tgl 9/21/99 */
		ExecDropTupleTable(tupleTable, true);
	}
#endif	 /* OMIT_PARTIAL_INDEX */

	pfree(nullv);
	pfree(datum);

	/*
	 * Since we just counted the tuples in the heap, we update its stats
	 * in pg_class to guarantee that the planner takes advantage of the
	 * index we just created.  But, only update statistics during normal
	 * index definitions, not for indices on system catalogs created
	 * during bootstrap processing.  We must close the relations before
	 * updating statistics to guarantee that the relcache entries are
	 * flushed when we increment the command counter in UpdateStats(). But
	 * we do not release any locks on the relations; those will be held
	 * until end of transaction.
	 */
	if (IsNormalProcessingMode())
	{
		Oid			hrelid = RelationGetRelid(heapRelation);
		Oid			irelid = RelationGetRelid(indexRelation);
		bool		inplace = IsReindexProcessing();

		heap_close(heapRelation, NoLock);
		index_close(indexRelation);
		UpdateStats(hrelid, reltuples);
		UpdateStats(irelid, indtuples);
		if (oldPred != NULL)
		{
			if (indtuples == reltuples)
				predicate = NULL;
			if (!inplace)
				UpdateIndexPredicate(irelid, oldPred, predicate);
		}
	}
}

/* ----------------
 *		index_build
 * ----------------
 */
void
index_build(Relation heapRelation,
			Relation indexRelation,
			int numberOfAttributes,
			AttrNumber *attributeNumber,
			uint16 parameterCount,
			Datum *parameter,
			FuncIndexInfo *funcInfo,
			PredInfo *predInfo)
{
	RegProcedure procedure;

	/* ----------------
	 *	sanity checks
	 * ----------------
	 */
	Assert(RelationIsValid(indexRelation));
	Assert(PointerIsValid(indexRelation->rd_am));

	procedure = indexRelation->rd_am->ambuild;

	/* ----------------
	 *	use the access method build procedure if supplied..
	 * ----------------
	 */
	if (RegProcedureIsValid(procedure))
		fmgr(procedure,
			 heapRelation,
			 indexRelation,
			 numberOfAttributes,
			 attributeNumber,
			 RelationGetIndexStrategy(indexRelation),
			 parameterCount,
			 parameter,
			 funcInfo,
			 predInfo);
	else
		DefaultBuild(heapRelation,
					 indexRelation,
					 numberOfAttributes,
					 attributeNumber,
					 RelationGetIndexStrategy(indexRelation),
					 parameterCount,
					 parameter,
					 funcInfo,
					 predInfo);
}
/*
 *    index_register() -- record an index that has been set up for building
 *                        later.
 *
 *        At bootstrap time, we define a bunch of indices on system catalogs.
 *        We postpone actually building the indices until just before we're
 *        finished with initialization, however.    This is because more classes
 *        and indices may be defined, and we want to be sure that all of them
 *        are present in the index.
 */
void
index_register(char *heap,
               char *ind,
               int natts,
               AttrNumber *attnos,
               uint16 nparams,
               Datum *params,
               FuncIndexInfo *finfo,
               PredInfo *predInfo)
{
    Datum       *v;
    IndexList  *newind;
    int            len;
    MemoryContext oldcxt;

    /*
     * XXX mao 10/31/92 -- don't gc index reldescs, associated info at
     * bootstrap time.    we'll declare the indices now, but want to create
     * them later.
     */

    if (nogc == NULL)
        nogc = AllocSetContextCreate((MemoryContext) NULL,
                                     "BootstrapNoGC",
                                     ALLOCSET_DEFAULT_MINSIZE,
                                     ALLOCSET_DEFAULT_INITSIZE,
                                     ALLOCSET_DEFAULT_MAXSIZE);

    oldcxt = MemoryContextSwitchTo((MemoryContext) nogc);

    newind = (IndexList *) palloc(sizeof(IndexList));
    newind->il_heap = pstrdup(heap);
    newind->il_ind = pstrdup(ind);
    newind->il_natts = natts;

    if (PointerIsValid(finfo))
        len = FIgetnArgs(finfo) * sizeof(AttrNumber);
    else
        len = natts * sizeof(AttrNumber);

    newind->il_attnos = (AttrNumber *) palloc(len);
    memmove(newind->il_attnos, attnos, len);

    if ((newind->il_nparams = nparams) > 0)
    {
        v = newind->il_params = (Datum *) palloc(2 * nparams * sizeof(Datum));
        nparams *= 2;
        while (nparams-- > 0)
        {
            *v = (Datum) palloc(strlen((char *) (*params)) + 1);
            strcpy((char *) *v++, (char *) *params++);
        }
    }
    else
        newind->il_params = (Datum *) NULL;

    if (finfo != (FuncIndexInfo *) NULL)
    {
        newind->il_finfo = (FuncIndexInfo *) palloc(sizeof(FuncIndexInfo));
        memmove(newind->il_finfo, finfo, sizeof(FuncIndexInfo));
    }
    else
        newind->il_finfo = (FuncIndexInfo *) NULL;

    if (predInfo != NULL)
    {
        newind->il_predInfo = (PredInfo *) palloc(sizeof(PredInfo));
        newind->il_predInfo->pred = predInfo->pred;
        newind->il_predInfo->oldPred = predInfo->oldPred;
    }
    else
        newind->il_predInfo = NULL;

    newind->il_next = ILHead;

    ILHead = newind;

    MemoryContextSwitchTo(oldcxt);
}

void
build_indices()
{
    Relation    heap;
    Relation    ind;

    for (; ILHead != (IndexList *) NULL; ILHead = ILHead->il_next)
    {
        heap = heap_openr(ILHead->il_heap, NoLock);
        Assert(heap);
        ind = index_openr(ILHead->il_ind);
        Assert(ind);
        index_build(heap, ind, ILHead->il_natts, ILHead->il_attnos,
                 ILHead->il_nparams, ILHead->il_params, ILHead->il_finfo,
                    ILHead->il_predInfo);

                 heap_close(heap, NoLock);
                 index_close(ind);

        /*
         * All of the rest of this routine is needed only because in
         * bootstrap processing we don't increment xact id's.  The normal
         * DefineIndex code replaces a pg_class tuple with updated info
         * including the relhasindex flag (which we need to have updated).
         * Unfortunately, there are always two indices defined on each
         * catalog causing us to update the same pg_class tuple twice for
         * each catalog getting an index during bootstrap resulting in the
         * ghost tuple problem (see heap_update).    To get around this we
         * change the relhasindex field ourselves in this routine keeping
         * track of what catalogs we already changed so that we don't
         * modify those tuples twice.  The normal mechanism for updating
         * pg_class is disabled during bootstrap.
         *
         * -mer
         */
        if (!BootstrapAlreadySeen(RelationGetRelid(heap)))
            UpdateStats(RelationGetRelid(heap), 0);

        /* XXX Probably we ought to close the heap and index here? */
    }
}

bool
BootstrapAlreadySeen(Oid id)
{
    static Oid    seenArray[MORE_THAN_THE_NUMBER_OF_CATALOGS];
    static int    nseen = 0;
    bool        seenthis;
    int            i;

    seenthis = false;

    for (i = 0; i < nseen; i++)
    {
        if (seenArray[i] == id)
        {
            seenthis = true;
            break;
        }
    }
    if (!seenthis)
    {
        seenArray[nseen] = id;
        nseen++;
    }
    return seenthis;
}

/*
 * IndexGetRelation: given an index's relation OID, get the OID of the
 * relation it is an index on.	Uses the system cache.
 */
Oid
IndexGetRelation(Oid indexId)
{
	HeapTuple	tuple;
	Form_pg_index index;

	tuple = SearchSysCacheTuple(INDEXRELID,
								ObjectIdGetDatum(indexId),
								0, 0, 0);
	if (!HeapTupleIsValid(tuple))
	{
		elog(ERROR, "IndexGetRelation: can't find index id %lu",
			 indexId);
	}
	index = (Form_pg_index) GETSTRUCT(tuple);
	Assert(index->indexrelid == indexId);

	return index->indrelid ;
}

/*
 * IndexIsUnique: given an index's relation OID, see if it
 * is unique using the system cache.
 */
char
IndexProperties(Oid indexId)
{
	HeapTuple	tuple;
	Form_pg_index index;
        IndexProp          result;

	tuple = SearchSysCacheTuple(INDEXRELID,
								ObjectIdGetDatum(indexId),
								0, 0, 0);
	if (!HeapTupleIsValid(tuple))
	{
		elog(ERROR, "IndexIsUnique: can't find index id %lu",
			 indexId);
	}
	index = (Form_pg_index) GETSTRUCT(tuple);
	Assert(index->indexrelid == indexId);
        
        result = index->indattributes;
        
        if ( index->indisunique ) {
            result |= INDEX_UNIQUE;
        }
        if ( index->indisprimary ) {
            result |= INDEX_PRIMARY;
        }
	return result;
}

/*
 * IndexIsUniqueNoCache: same as above function, but don't use the
 * system cache.  if we are called from btbuild, the transaction
 * that is adding the entry to pg_index has not been committed yet.
 * the system cache functions will do a heap scan, but only with
 * NowTimeQual, not SelfTimeQual, so it won't find tuples added
 * by the current transaction (which is good, because if the transaction
 * is aborted, you don't want the tuples sitting around in the cache).
 * so anyway, we have to do our own scan with SelfTimeQual.
 * this is only called when a new index is created, so it's OK
 * if it's slow.
 */
bool
IndexIsUniqueNoCache(Oid indexId)
{
	Relation	pg_index;
	ScanKeyData skey[1];
	HeapScanDesc scandesc;
	HeapTuple	tuple;
	Form_pg_index index;
	bool		isunique;

	pg_index = heap_openr(IndexRelationName, AccessShareLock);

	ScanKeyEntryInitialize(&skey[0], (bits16) 0x0,
						   Anum_pg_index_indexrelid,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(indexId));

	scandesc = heap_beginscan(pg_index, SnapshotSelf, 1, skey);

	/* NO CACHE */
	tuple = heap_getnext(scandesc);
	if (!HeapTupleIsValid(tuple)) {
            heap_endscan(scandesc);
            heap_close(pg_index, AccessShareLock);
            elog(ERROR, "IndexIsUniqueNoCache: can't find index id %lu", indexId);
        }

	index = (Form_pg_index) GETSTRUCT(tuple);
	Assert(index->indexrelid == indexId);
	isunique = index->indisunique;

	heap_endscan(scandesc);
	heap_close(pg_index, AccessShareLock);
	return isunique;
}


/* ---------------------------------
 * activate_index -- activate/deactivate the specified index.
 *		Note that currelntly PostgreSQL doesn't hold the
 *		status per index
 * ---------------------------------
 */
bool
activate_index(Oid indexId, bool activate)
{
	if (!activate)				/* Currently does nothing */
		return true;
	return reindex_index(indexId, false);
}

/* --------------------------------
 * reindex_index - This routine is used to recreate an index
 * --------------------------------
 */
bool
reindex_index(Oid indexId, bool force)
{
	Relation	iRel,
				indexRelation,
				heapRelation;
	ScanKeyData entry;
	HeapScanDesc scan;
	HeapTuple	indexTuple,
				procTuple,
				classTuple;
	Form_pg_index index;
	Oid			heapId,
				procId,
				accessMethodId;
	Node	   *oldPred = NULL;
	PredInfo   *predInfo;
	AttrNumber *attributeNumberA;
	FuncIndexInfo fInfo,
			   *funcInfo = NULL;
	int			i,
				numberOfAttributes;
	char	   *predString;
	bool		old;

	old = SetReindexProcessing(true);
	/* Scan pg_index to find indexes on heapRelation */
	indexRelation = heap_openr(IndexRelationName, AccessShareLock);
	ScanKeyEntryInitialize(&entry, 0, Anum_pg_index_indexrelid, F_OIDEQ,
						   ObjectIdGetDatum(indexId));
	scan = heap_beginscan(indexRelation, SnapshotNow, 1, &entry);
	indexTuple = heap_getnext(scan);
	if (!HeapTupleIsValid(indexTuple)) {
                heap_endscan(scan);
                heap_close(indexRelation, AccessShareLock);
		elog(ERROR, "reindex_index index %ld tuple is invalid", indexId);
        }

	/*
	 * For the index, fetch index attributes so we can apply index_build
	 */
	index = (Form_pg_index) GETSTRUCT(indexTuple);
	heapId = index->indrelid;
	procId = index->indproc;

	for (i = 0; i < INDEX_MAX_KEYS; i++)
	{
		if (index->indkey[i] == InvalidAttrNumber)
			break;
	}
	numberOfAttributes = i;

	/* If a valid where predicate, compute predicate Node */
	if (VARSIZE(&index->indpred) != 0)
	{
		predString = fmgr(F_TEXTOUT, &index->indpred);
		oldPred = stringToNode(predString);
		pfree(predString);
	}
	predInfo = (PredInfo *) palloc(sizeof(PredInfo));
	predInfo->pred = (Node *) oldPred;
	predInfo->oldPred = NULL;

	/* Assign Index keys to attributes array */
	attributeNumberA = (AttrNumber *) palloc(numberOfAttributes * sizeof(AttrNumber));
	for (i = 0; i < numberOfAttributes; i++)
		attributeNumberA[i] = index->indkey[i];

	/* If this is a procedural index, initialize our FuncIndexInfo */
	if (procId != InvalidOid)
	{
		funcInfo = &fInfo;
		FIsetnArgs(funcInfo, numberOfAttributes);
		procTuple = SearchSysCacheTuple(PROCOID, ObjectIdGetDatum(procId),
										0, 0, 0);
		if (!HeapTupleIsValid(procTuple))
			elog(ERROR, "RelationTruncateIndexes: index procedure not found");
		namecpy(&(funcInfo->funcName),
				&(((Form_pg_proc) GETSTRUCT(procTuple))->proname));
		FIsetProcOid(funcInfo, procTuple->t_data->t_oid);
	}

	/* Fetch the classTuple associated with this index */
	classTuple = SearchSysCacheTupleCopy(RELOID, ObjectIdGetDatum(indexId), 0, 0, 0);
	if (!HeapTupleIsValid(classTuple))
		elog(ERROR, "RelationTruncateIndexes: index access method not found");
	accessMethodId = ((Form_pg_class) GETSTRUCT(classTuple))->relam;

	/* Open our index relation */
	iRel = index_open(indexId);
	if (iRel == NULL)
		elog(ERROR, "reindex_index: can't open index relation");
	heapRelation = heap_open(heapId, ExclusiveLock);
	if (heapRelation == NULL)
		elog(ERROR, "reindex_index: can't open heap relation");

	/* Obtain exclusive lock on it, just to be sure */
	LockRelation(iRel, AccessExclusiveLock);

	/*
	 * Release any buffers associated with this index.	If they're dirty,
	 * they're just dropped without bothering to flush to disk.
	 */
	InvalidateRelationBuffers(iRel);
	/* Now truncate the actual data and set blocks to zero */
        ForgetFreespace(iRel,false);
	smgrtruncate(iRel->rd_smgr, 0);
	iRel->rd_nblocks = 0;

	/* Initialize the index and rebuild */
	InitIndexStrategy(numberOfAttributes, iRel, accessMethodId);
	index_build(heapRelation, iRel, numberOfAttributes,
				attributeNumberA, 0, NULL, funcInfo, predInfo);

	/*
	 * index_build will close both the heap and index relations (but not
	 * give up the locks we hold on them).	That's fine for the index, but
	 * we need to open the heap again.	We need no new lock, since this
	 * backend still has the exclusive lock grabbed by heap_truncate.
	 */
         /*  Why? MKS  7.15.2006
	iRel = index_open(indexId);
	Assert(iRel != NULL);
        */
	/* Complete the scan and close pg_index */
	heap_endscan(scan);
	heap_close(indexRelation, AccessShareLock);
	SetReindexProcessing(old);
	return true;
}

/*
 * ----------------------------
 * activate_indexes_of_a_table
 *	activate/deactivate indexes of the specified table.
 * ----------------------------
 */
bool
activate_indexes_of_a_table(Oid relid, bool activate)
{
	if (IndexesAreActive(relid, true))
	{
		if (!activate)
			setRelhasindexInplace(relid, false, true);
		else
			return false;
	}
	else
	{
		if (activate)
			reindex_relation(relid, true);
		else
			return false;
	}
	return true;
}

/* --------------------------------
 * reindex_relation - This routine is used to recreate indexes
 * of a relation.
 * --------------------------------
 */
bool
reindex_relation(Oid relid, bool force)
{
	Relation	indexRelation;
	ScanKeyData entry;
	HeapScanDesc scan;
	HeapTuple	indexTuple;
	bool		old,
				reindexed;

	old = SetReindexProcessing(true);
	if (IndexesAreActive(relid, true))
	{
		if (!force)
		{
			SetReindexProcessing(old);
			return false;
		}
		activate_indexes_of_a_table(relid, false);
	}

	indexRelation = heap_openr(IndexRelationName, AccessShareLock);
	ScanKeyEntryInitialize(&entry, 0, Anum_pg_index_indrelid,
						   F_OIDEQ, ObjectIdGetDatum(relid));
	scan = heap_beginscan(indexRelation, SnapshotNow, 1, &entry);
	reindexed = false;
	while (HeapTupleIsValid(indexTuple = heap_getnext(scan)))
	{
		Form_pg_index index = (Form_pg_index) GETSTRUCT(indexTuple);

		if (activate_index(index->indexrelid, true))
			reindexed = true;
		else
		{
			reindexed = false;
			break;
		}
	}
	heap_endscan(scan);
	heap_close(indexRelation, AccessShareLock);
	if (reindexed)
		setRelhasindexInplace(relid, true, false);
	SetReindexProcessing(old);
	return reindexed;
}
#ifdef NOTUSED

/*
 * unable to store NULLs.
 */
double
IndexBuildHeapScan(Relation heapRelation,
				   Relation indexRelation,
				   PredInfo *predInfo,
				   IndexBuildCallback callback,
				   void *callback_state)
{
	HeapScanDesc scan;
	HeapTuple	heapTuple;
	TupleDesc	heapDescriptor;
	Datum		attdata[INDEX_MAX_KEYS];
	char		nulls[INDEX_MAX_KEYS];
	double		reltuples;
	TupleTable	tupleTable;
	TupleTableSlot *slot;
	ExprContext *econtext;
	Snapshot	snapshot;
	TransactionId OldestXmin;

	/*
	 * sanity checks
	 */
	Assert(OidIsValid(indexRelation->rd_rel->relam));

	heapDescriptor = RelationGetDescr(heapRelation);

	/*
	 * If this is a predicate (partial) index, we will need to evaluate
	 * the predicate using ExecQual, which requires the current tuple to
	 * be in a slot of a TupleTable.  In addition, ExecQual must have an
	 * ExprContext referring to that slot.	Here, we initialize dummy
	 * TupleTable and ExprContext objects for this purpose. --Nels, Feb 92
	 *
	 * We construct the ExprContext anyway since we need a per-tuple
	 * temporary memory context for function evaluation -- tgl July 00
	 */
#ifdef FUTURE_PLAN
	if (predInfo != NULL)
	{
		tupleTable = ExecCreateTupleTable(1);
		slot = ExecAllocTableSlot(tupleTable);
		ExecSetSlotDescriptor(slot, heapDescriptor);
	}
	else
#endif
	{
		tupleTable = NULL;
		slot = NULL;
	}
#ifdef FUTURE_PLAN
	econtext = MakeExprContext(slot, TransactionCommandContext);
#endif
	/*
	 * Ok, begin our scan of the base relation.  We use SnapshotAny
	 * because we must retrieve all tuples and do our own time qual
	 * checks.
	 */
	if (IsBootstrapProcessingMode())
	{
		snapshot = SnapshotNow;
		OldestXmin = InvalidTransactionId;
	}
	else
	{
		snapshot = SnapshotAny;
		OldestXmin = GetOldestXmin(heapRelation->rd_rel->relisshared);
	}

	scan = heap_beginscan(heapRelation, /* relation */
						  snapshot,		/* seeself */
						  0,	/* number of keys */
						  (ScanKey) NULL);		/* scan key */

	reltuples = 0;

	/*
	 * Scan all tuples in the base relation.
	 */
	while ((heapTuple = heap_getnext(scan)) != NULL)
	{
		bool		tupleIsAlive;
        bool        saveinfo = FALSE;

		CHECK_FOR_INTERRUPTS();

		if (snapshot == SnapshotAny)
		{
			/* do our own time qual check */
			bool		indexIt;
			uint16		sv_infomask;

			/*
			 * HeapTupleSatisfiesVacuum may update tuple's hint status
			 * bits. We could possibly get away with not locking the
			 * buffer here, since caller should hold ShareLock on the
			 * relation, but let's be conservative about it.
			 */
			LockBuffer((heapRelation), scan->rs_cbuf, BUFFER_LOCK_SHARE);
			sv_infomask = heapTuple->t_data->t_infomask;

			switch (HeapTupleSatisfiesVacuum(heapTuple->t_data, OldestXmin))
			{
				case HEAPTUPLE_DEAD:
				case HEAPTUPLE_STILLBORN:
					indexIt = false;
					tupleIsAlive = false;
					break;
				case HEAPTUPLE_LIVE:
					indexIt = true;
					tupleIsAlive = true;
					break;
				case HEAPTUPLE_RECENTLY_DEAD:

					/*
					 * If tuple is recently deleted then we must index it
					 * anyway to keep VACUUM from complaining.
					 */
					indexIt = true;
					tupleIsAlive = false;
					break;
				case HEAPTUPLE_INSERT_IN_PROGRESS:

					/*
					 * Since caller should hold ShareLock or better, we
					 * should not see any tuples inserted by open
					 * transactions --- unless it's our own transaction.
					 * (Consider INSERT followed by CREATE INDEX within a
					 * transaction.)
					 */
					if (!TransactionIdIsCurrentTransactionId(
							  HeapTupleHeaderGetXmin(heapTuple->t_data)))
						elog(ERROR, "IndexBuildHeapScan: concurrent insert in progress");
					indexIt = true;
					tupleIsAlive = true;
					break;
				case HEAPTUPLE_DELETE_IN_PROGRESS:

					/*
					 * Since caller should hold ShareLock or better, we
					 * should not see any tuples deleted by open
					 * transactions --- unless it's our own transaction.
					 * (Consider DELETE followed by CREATE INDEX within a
					 * transaction.)
					 */
					if (!TransactionIdIsCurrentTransactionId(
							  HeapTupleHeaderGetXmax(heapTuple->t_data)))
						elog(ERROR, "IndexBuildHeapScan: concurrent delete in progress");
					indexIt = true;
					tupleIsAlive = false;
					break;
				default:
					elog(ERROR, "Unexpected HeapTupleSatisfiesVacuum result");
					indexIt = tupleIsAlive = false;		/* keep compiler quiet */
					break;
			}

			/* check for hint-bit update by HeapTupleSatisfiesVacuum */
			if (sv_infomask != heapTuple->t_data->t_infomask)
                saveinfo = TRUE;
                
			LockBuffer((heapRelation), scan->rs_cbuf, BUFFER_LOCK_UNLOCK);

            if ( saveinfo ) 
                SetBufferCommitInfoNeedsSave(scan->rs_cbuf);

			if (!indexIt)
				continue;
		}
		else
		{
			/* heap_getnext did the time qual check */
			tupleIsAlive = true;
		}

		reltuples += 1;
/*
		MemoryContextReset(econtext->ecxt_per_tuple_memory);
*/
		/*
		 * In a partial index, discard tuples that don't satisfy the
		 * predicate.  We can also discard recently-dead tuples, since
		 * VACUUM doesn't complain about tuple count mismatch for partial
		 * indexes.
		 */
#ifdef FUTURE_PLAN
		if (predInfo != NIL)
		{
			if (!tupleIsAlive)
				continue;
			ExecStoreTuple(heapTuple, slot,false);
			if (!ExecQual(predicate, econtext, false))
				continue;
		}
#endif
		/*
		 * For the current heap tuple, extract all the attributes we use
		 * in this index, and note which are null.	This also performs
		 * evaluation of the function, if this is a functional index.
		 */
		FormIndexDatum(indexInfo,
					   heapTuple,
					   heapDescriptor,
					   econtext->ecxt_per_tuple_memory,
					   attdata,
					   nulls);

		/*
		 * You'd think we should go ahead and build the index tuple here,
		 * but some index AMs want to do further processing on the data
		 * first.  So pass the attdata and nulls arrays, instead.
		 */

		/* Call the AM's callback routine to process the tuple */
		callback(indexRelation, heapTuple, attdata, nulls, tupleIsAlive,
				 callback_state);
	}

	heap_endscan(scan);
#ifdef FUTURE_PLAN
	if (predInfo != NIL)
		ExecDropTupleTable(tupleTable, true);
	FreeExprContext(econtext);
#endif
	return reltuples;
}
#endif
