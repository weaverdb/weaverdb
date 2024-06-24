/*-------------------------------------------------------------------------
 *
 * indexing.c
 *	  This file contains routines to support indices defined on system
 *	  catalogs.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/catalog/indexing.c,v 1.1.1.1 2006/08/12 00:20:12 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */


#include "postgres.h"
#include "env/env.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "catalog/catalog.h"
#include "catalog/catname.h"
#include "catalog/index.h"
#include "catalog/indexing.h"
#include "catalog/pg_index.h"
#include "miscadmin.h"
#include "utils/syscache.h"

/*
 * Names of indices - they match all system caches
 */

char	   *Name_pg_aggregate_indices[Num_pg_aggregate_indices] =
{AggregateNameTypeIndex};
char	   *Name_pg_am_indices[Num_pg_am_indices] =
{AmNameIndex};
char	   *Name_pg_amop_indices[Num_pg_amop_indices] =
{AccessMethodOpidIndex, AccessMethodStrategyIndex};
char	   *Name_pg_attr_indices[Num_pg_attr_indices] =
{AttributeRelidNameIndex, AttributeRelidNumIndex};
char	   *Name_pg_attrdef_indices[Num_pg_attrdef_indices] =
{AttrDefaultIndex};
char	   *Name_pg_class_indices[Num_pg_class_indices] =
{ClassNameIndex, ClassOidIndex};
char	   *Name_pg_extstore_indices[Num_pg_extstore_indices] =
{ExtStoreAttnumIndex};
char	   *Name_pg_group_indices[Num_pg_group_indices] =
{GroupNameIndex, GroupSysidIndex};
char	   *Name_pg_index_indices[Num_pg_index_indices] =
{IndexRelidIndex};
char	   *Name_pg_inherits_indices[Num_pg_inherits_indices] =
{InheritsRelidSeqnoIndex};
char	   *Name_pg_language_indices[Num_pg_language_indices] =
{LanguageOidIndex, LanguageNameIndex};
char	   *Name_pg_listener_indices[Num_pg_listener_indices] =
{ListenerRelnamePidIndex};
char	   *Name_pg_opclass_indices[Num_pg_opclass_indices] =
{OpclassNameIndex, OpclassDeftypeIndex};
char	   *Name_pg_operator_indices[Num_pg_operator_indices] =
{OperatorOidIndex, OperatorNameIndex};
char	   *Name_pg_proc_indices[Num_pg_proc_indices] =
{ProcedureOidIndex, ProcedureNameIndex};
char	   *Name_pg_relcheck_indices[Num_pg_relcheck_indices] =
{RelCheckIndex};
char	   *Name_pg_rewrite_indices[Num_pg_rewrite_indices] =
{RewriteOidIndex, RewriteRulenameIndex};
char	   *Name_pg_shadow_indices[Num_pg_shadow_indices] =
{ShadowNameIndex, ShadowSysidIndex};
char	   *Name_pg_statistic_indices[Num_pg_statistic_indices] =
{StatisticRelidAttnumIndex};
char	   *Name_pg_trigger_indices[Num_pg_trigger_indices] =
{TriggerRelidIndex, TriggerConstrNameIndex, TriggerConstrRelidIndex};
char	   *Name_pg_type_indices[Num_pg_type_indices] =
{TypeNameIndex, TypeOidIndex};
char	   *Name_pg_description_indices[Num_pg_description_indices] =
{DescriptionObjIndex};



static HeapTuple CatalogIndexFetchTuple(Relation heapRelation,
					   Relation idesc,
					   ScanKey skey,
					   int16 num_keys);


/*
 * Changes (appends) to catalogs can (and does) happen at various places
 * throughout the code.  We need a generic routine that will open all of
 * the indices defined on a given catalog a return the relation descriptors
 * associated with them.
 */
void
CatalogOpenIndices(int nIndices, char **names, Relation *idescs)
{
	int			i;

	if (IsIgnoringSystemIndexes())
		return;
	for (i = 0; i < nIndices; i++)
		idescs[i] = index_openr(names[i]);
}

/*
 * This is the inverse routine to CatalogOpenIndices()
 */
void
CatalogCloseIndices(int nIndices, Relation *idescs)
{
	int			i;

	if (IsIgnoringSystemIndexes())
		return;
	for (i = 0; i < nIndices; i++)
		index_close(idescs[i]);
}


/*
 * For the same reasons outlined above CatalogOpenIndices() we need a routine
 * that takes a new catalog tuple and inserts an associated index tuple into
 * each catalog index.
 */
void
CatalogIndexInsert(Relation *idescs,
				   int nIndices,
				   Relation heapRelation,
				   HeapTuple heapTuple)
{
	HeapTuple	index_tup;
	TupleDesc	heapDescriptor;
	Form_pg_index index_form;
	Datum		datum[INDEX_MAX_KEYS];
	char		nulls[INDEX_MAX_KEYS];
	int			natts;
	AttrNumber *attnumP;
	FuncIndexInfo finfo,
			   *finfoP;
	int			i;

	if (IsIgnoringSystemIndexes())
		return;
	heapDescriptor = RelationGetDescr(heapRelation);

	for (i = 0; i < nIndices; i++)
	{
		InsertIndexResult indexRes;

		index_tup = SearchSysCacheTupleCopy(INDEXRELID,
									  ObjectIdGetDatum(idescs[i]->rd_id),
											0, 0, 0);
		Assert(index_tup);
		index_form = (Form_pg_index) GETSTRUCT(index_tup);

		if (index_form->indproc != InvalidOid)
		{
			int			fatts;

			/*
			 * Compute the number of attributes we are indexing upon.
			 */
			for (attnumP = index_form->indkey, fatts = 0;
				 fatts < INDEX_MAX_KEYS && *attnumP != InvalidAttrNumber;
				 attnumP++, fatts++)
				;
			FIgetnArgs(&finfo) = fatts;
			natts = 1;
			FIgetProcOid(&finfo) = index_form->indproc;
			*(FIgetname(&finfo)) = '\0';
			finfoP = &finfo;
		}
		else
		{
			natts = RelationGetDescr(idescs[i])->natts;
			finfoP = (FuncIndexInfo *) NULL;
		}

		FormIndexDatum(natts,
					   (AttrNumber *) index_form->indkey,
					   heapTuple,
					   heapDescriptor,
					   datum,
					   nulls,
					   finfoP);

		indexRes = index_insert(idescs[i], datum, nulls,
								&heapTuple->t_self, heapRelation, false);
		if (indexRes)
			pfree(indexRes);

		heap_freetuple(index_tup);
	}
}

/*
 * This is needed at initialization when reldescs for some of the crucial
 * system catalogs are created and nailed into the cache.
 */
bool
CatalogHasIndex(char *catName, Oid catId)
{
	Relation	pg_class;
	HeapTuple	htup;
	Form_pg_class pgRelP;
	int			i;

	Assert(IsSystemRelationName(catName));

	/*
	 * If we're bootstraping we don't have pg_class (or any indices).
	 */
	if (IsBootstrapProcessingMode())
		return false;

	if (IsInitProcessingMode())
	{
		for (i = 0; IndexedCatalogNames[i] != NULL; i++)
		{
			if (strcmp(IndexedCatalogNames[i], catName) == 0)
				return true;
		}
		return false;
	}

	pg_class = heap_openr(RelationRelationName, AccessShareLock);
	htup = ClassOidIndexScan(pg_class, catId);
	heap_close(pg_class, AccessShareLock);

	if (!HeapTupleIsValid(htup))
	{
		elog(NOTICE, "CatalogHasIndex: no relation with oid %lu", catId);
		return false;
	}

	pgRelP = (Form_pg_class) GETSTRUCT(htup);
	return pgRelP->relhasindex;
}


/*
 *	CatalogIndexFetchTuple() -- Get a tuple that satisfies a scan key
 *								from a catalog relation.
 *
 *		Since the index may contain pointers to dead tuples, we need to
 *		iterate until we find a tuple that's valid and satisfies the scan
 *		key.
 */
static HeapTuple
CatalogIndexFetchTuple(Relation heapRelation,
					   Relation idesc,
					   ScanKey skey,
					   int16 num_keys)
{
	IndexScanDesc sd;
	HeapTupleData tuple;
	HeapTuple	result = NULL;
	Buffer		buffer;

	tuple.t_data = NULL;
	tuple.t_datamcxt = NULL;
	tuple.t_datasrc = NULL;
        tuple.t_info = 0;
	ItemPointerSetInvalid(&tuple.t_self);
	tuple.t_len = 0;

	sd = index_beginscan(idesc, false, num_keys, skey);

	while (index_getnext(sd, ForwardScanDirection))
	{
		tuple.t_self = sd->xs_ctup.t_self;
		if ( heap_fetch(heapRelation, SnapshotNow, &tuple, &buffer) ) {
			result = heap_copytuple(&tuple);
			ReleaseBuffer(heapRelation,buffer);
			break;
		}
	}

	index_endscan(sd);

	return result;
}


/*---------------------------------------------------------------------
 *						 Class-specific index lookups
 *---------------------------------------------------------------------
 */

/*
 * The remainder of the file is for individual index scan routines.  Each
 * index should be scanned according to how it was defined during bootstrap
 * (that is, functional or normal) and what arguments the cache lookup
 * requires.  Each routine returns the heap tuple that qualifies.
 */


HeapTuple
AggregateNameTypeIndexScan(Relation heapRelation, ...)
{
	Relation	idesc;
	ScanKeyData skey[2];
	HeapTuple	tuple;
        va_list         args;
        char *aggName;
        Oid aggType;

        va_start(args, heapRelation);
        aggName = DatumGetPointer(va_arg(args, Datum));
        aggType = DatumGetObjectId(va_arg(args, Datum));
	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_NAMEEQ,
						   PointerGetDatum(aggName));

	ScanKeyEntryInitialize(&skey[1],
						   (bits16) 0x0,
						   (AttrNumber) 2,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(aggType));

	idesc = index_openr(AggregateNameTypeIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 2);

	index_close(idesc);
        va_end(args);
	return tuple;
}


HeapTuple
AmNameIndexScan(Relation heapRelation, ...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;
        
        va_list         args;
        char *amName;

        va_start(args, heapRelation);
        amName = DatumGetPointer(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_NAMEEQ,
						   PointerGetDatum(amName));

	idesc = index_openr(AmNameIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);
        va_end(args);
	return tuple;
}


HeapTuple
AccessMethodOpidIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[3];
	HeapTuple	tuple;
        
        va_list args;
        Oid claid;
        Oid opopr;
        Oid opid;

        va_start(args, heapRelation);
        claid = DatumGetObjectId(va_arg(args, Datum));
        opopr = DatumGetObjectId(va_arg(args, Datum));
        opid = DatumGetObjectId(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(claid));

	ScanKeyEntryInitialize(&skey[1],
						   (bits16) 0x0,
						   (AttrNumber) 2,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(opopr));

	ScanKeyEntryInitialize(&skey[2],
						   (bits16) 0x0,
						   (AttrNumber) 3,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(opid));

	idesc = index_openr(AccessMethodOpidIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 3);

	index_close(idesc);
        
        va_end(args);

	return tuple;
}

HeapTuple
AccessMethodStrategyIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[3];
	HeapTuple	tuple;

        va_list args;
        
        va_start(args, heapRelation);
        Oid opid = DatumGetObjectId(va_arg(args, Datum));
	Oid claid = DatumGetObjectId(va_arg(args, Datum));
	int2 opstrategy = DatumGetInt16(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(opid));

	ScanKeyEntryInitialize(&skey[1],
						   (bits16) 0x0,
						   (AttrNumber) 2,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(claid));

	ScanKeyEntryInitialize(&skey[2],
						   (bits16) 0x0,
						   (AttrNumber) 3,
						   (RegProcedure) F_INT2EQ,
						   Int16GetDatum(opstrategy));

	idesc = index_openr(AccessMethodStrategyIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 3);

	index_close(idesc);
        va_end(args);

	return tuple;
}


HeapTuple
AttributeRelidNameIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[2];
	HeapTuple	tuple;

        va_list args;

        va_start(args, heapRelation);

        Oid relid = DatumGetObjectId(va_arg(args, Datum));
	char *attname = DatumGetPointer(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(relid));

	ScanKeyEntryInitialize(&skey[1],
						   (bits16) 0x0,
						   (AttrNumber) 2,
						   (RegProcedure) F_NAMEEQ,
						   NameGetDatum(attname));

	idesc = index_openr(AttributeRelidNameIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 2);

	index_close(idesc);

        va_end(args);

	return tuple;
}


HeapTuple
AttributeRelidNumIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[2];
	HeapTuple	tuple;
        
        va_list args;
        va_start(args, heapRelation);

	Oid relid = DatumGetObjectId(va_arg(args, Datum));
        AttrNumber attnum = DatumGetInt16(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(relid));

	ScanKeyEntryInitialize(&skey[1],
						   (bits16) 0x0,
						   (AttrNumber) 2,
						   (RegProcedure) F_INT2EQ,
						   Int16GetDatum(attnum));

	idesc = index_openr(AttributeRelidNumIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 2);

	index_close(idesc);

        va_end(args);
	return tuple;
}


HeapTuple
OpclassDeftypeIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;
        va_list     args;

        va_start(args, heapRelation);
        Oid defType = DatumGetObjectId(va_arg(args,Datum));
        
	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(defType));

	idesc = index_openr(OpclassDeftypeIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);

        va_end(args);

	return tuple;
}


HeapTuple
OpclassNameIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;

        va_list args;

        va_start(args, heapRelation);
        char *opcName = DatumGetPointer(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_NAMEEQ,
						   PointerGetDatum(opcName));

	idesc = index_openr(OpclassNameIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);

        va_end(args);
	return tuple;
}


HeapTuple
GroupNameIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;

        va_list args;

        va_start(args, heapRelation);
        char *groName = DatumGetPointer(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_NAMEEQ,
						   PointerGetDatum(groName));

	idesc = index_openr(GroupNameIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);

        va_end(args);
	return tuple;
}


HeapTuple
GroupSysidIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;

        va_list args;

        va_start(args, heapRelation);
        int4 sysId = DatumGetInt32(va_arg(args, Datum));
 
	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_INT4EQ,
						   Int32GetDatum(sysId));

	idesc = index_openr(GroupSysidIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);

        va_end(args);
	return tuple;
}


HeapTuple
IndexRelidIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;

        va_list args;

        va_start(args, heapRelation);
        Oid relid = DatumGetObjectId(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(relid));

	idesc = index_openr(IndexRelidIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);

        va_end(args);
	return tuple;
}

HeapTuple
ExtStoreAttnumIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[2];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        Oid relid = DatumGetObjectId(va_arg(args, Datum));
        int4 attnum = DatumGetInt32(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(relid));

	ScanKeyEntryInitialize(&skey[1],
						   (bits16) 0x0,
						   (AttrNumber) 2,
						   (RegProcedure) F_INT2EQ,
						   Int16GetDatum(attnum));

	idesc = index_openr(ExtStoreAttnumIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 2);

	index_close(idesc);

        va_end(args);
	return tuple;
}

HeapTuple
InheritsRelidSeqnoIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[2];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        Oid relid = DatumGetObjectId(va_arg(args, Datum));
        int4 seqno = DatumGetInt32(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(relid));

	ScanKeyEntryInitialize(&skey[1],
						   (bits16) 0x0,
						   (AttrNumber) 2,
						   (RegProcedure) F_INT4EQ,
						   Int32GetDatum(seqno));

	idesc = index_openr(InheritsRelidSeqnoIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 2);

	index_close(idesc);

        va_end(args);
	return tuple;
}


HeapTuple
LanguageNameIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        char *lanName = DatumGetPointer(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_NAMEEQ,
						   PointerGetDatum(lanName));

	idesc = index_openr(LanguageNameIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);
        va_end(args);
	return tuple;
}


HeapTuple
LanguageOidIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        Oid lanId = DatumGetObjectId(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(lanId));

	idesc = index_openr(LanguageOidIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);

	return tuple;
}


HeapTuple
ListenerRelnamePidIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[2];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        char *relName = DatumGetPointer(va_arg(args, Datum));
        int4 pid = DatumGetInt32(va_arg(args, Datum));
	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_NAMEEQ,
						   PointerGetDatum(relName));

	ScanKeyEntryInitialize(&skey[1],
						   (bits16) 0x0,
						   (AttrNumber) 2,
						   (RegProcedure) F_INT4EQ,
						   Int32GetDatum(pid));

	idesc = index_openr(ListenerRelnamePidIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 2);

	index_close(idesc);
        va_end(args);
	return tuple;
}


HeapTuple
OperatorNameIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[4];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        char *oprName = DatumGetPointer(va_arg(args, Datum));
        Oid oprLeft = DatumGetObjectId(va_arg(args, Datum));
        Oid oprRight = DatumGetObjectId(va_arg(args, Datum));
        char oprKind = DatumGetChar(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_NAMEEQ,
						   PointerGetDatum(oprName));

	ScanKeyEntryInitialize(&skey[1],
						   (bits16) 0x0,
						   (AttrNumber) 2,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(oprLeft));

	ScanKeyEntryInitialize(&skey[2],
						   (bits16) 0x0,
						   (AttrNumber) 3,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(oprRight));

	ScanKeyEntryInitialize(&skey[3],
						   (bits16) 0x0,
						   (AttrNumber) 4,
						   (RegProcedure) F_CHAREQ,
						   CharGetDatum(oprKind));

	idesc = index_openr(OperatorNameIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 4);

	index_close(idesc);
        va_end(args);

	return tuple;
}


HeapTuple
OperatorOidIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        Oid oprId = DatumGetObjectId(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(oprId));

	idesc = index_openr(OperatorOidIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);
        va_end(args);
	return tuple;
}


HeapTuple
ProcedureNameIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[3];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        char *procName = DatumGetPointer(va_arg(args, Datum));
        int2 nargs = DatumGetInt16(va_arg(args, Datum));
        void *argTypes = DatumGetPointer(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_NAMEEQ,
						   PointerGetDatum(procName));

	ScanKeyEntryInitialize(&skey[1],
						   (bits16) 0x0,
						   (AttrNumber) 2,
						   (RegProcedure) F_INT2EQ,
						   Int16GetDatum(nargs));

	ScanKeyEntryInitialize(&skey[2],
						   (bits16) 0x0,
						   (AttrNumber) 3,
						   (RegProcedure) F_OIDVECTOREQ,
						   PointerGetDatum(argTypes));

	idesc = index_openr(ProcedureNameIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 3);

	index_close(idesc);
        
        va_end(args);
	return tuple;
}


HeapTuple
ProcedureOidIndexScan(Relation heapRelation, ...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        Oid procId = DatumGetObjectId(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(procId));

	idesc = index_openr(ProcedureOidIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);
        va_end(args);
	return tuple;
}


HeapTuple
ClassNameIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        char *relName = DatumGetPointer(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_NAMEEQ,
						   PointerGetDatum(relName));

	idesc = index_openr(ClassNameIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);
        va_end(args);
	return tuple;
}


HeapTuple
ClassOidIndexScan(Relation heapRelation, ...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        Oid relId = DatumGetObjectId(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(relId));

	idesc = index_openr(ClassOidIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);
        va_end(args);
	return tuple;
}


HeapTuple
RewriteRulenameIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        char* ruleName = DatumGetPointer(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_NAMEEQ,
						   PointerGetDatum(ruleName));

	idesc = index_openr(RewriteRulenameIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);
        va_end(args);
	return tuple;
}


HeapTuple
RewriteOidIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        Oid rewriteId = DatumGetObjectId(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(rewriteId));

	idesc = index_openr(RewriteOidIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);
        va_end(args);
	return tuple;
}


HeapTuple
ShadowNameIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
         char *useName = DatumGetPointer(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_NAMEEQ,
						   PointerGetDatum(useName));

	idesc = index_openr(ShadowNameIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);
	return tuple;
}


HeapTuple
ShadowSysidIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        int4 sysId = DatumGetInt32(va_arg(args, Datum));
	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_INT4EQ,
						   Int32GetDatum(sysId));

	idesc = index_openr(ShadowSysidIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);
        va_end(args);
	return tuple;
}


HeapTuple
StatisticRelidAttnumIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[2];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        Oid relId = DatumGetObjectId(va_arg(args, Datum));
        AttrNumber attNum = DatumGetInt16(va_arg(args, Datum));
	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(relId));

	ScanKeyEntryInitialize(&skey[1],
						   (bits16) 0x0,
						   (AttrNumber) 2,
						   (RegProcedure) F_INT2EQ,
						   Int16GetDatum(attNum));

	idesc = index_openr(StatisticRelidAttnumIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 2);

	index_close(idesc);
        va_end(args);
	return tuple;
}


HeapTuple
TypeNameIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        char *typeName = DatumGetPointer(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_NAMEEQ,
						   PointerGetDatum(typeName));

	idesc = index_openr(TypeNameIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);
        va_end(args);
	return tuple;
}


HeapTuple
TypeOidIndexScan(Relation heapRelation,...)
{
	Relation	idesc;
	ScanKeyData skey[1];
	HeapTuple	tuple;
        va_list args;

        va_start(args, heapRelation);
        Oid typeId = DatumGetObjectId(va_arg(args, Datum));

	ScanKeyEntryInitialize(&skey[0],
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(typeId));

	idesc = index_openr(TypeOidIndex);
	tuple = CatalogIndexFetchTuple(heapRelation, idesc, skey, 1);

	index_close(idesc);
        va_end(args);
	return tuple;
}
