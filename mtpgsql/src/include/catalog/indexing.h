/*-------------------------------------------------------------------------
 *
 * indexing.h
 *	  This include provides some definitions to support indexing
 *	  on system catalogs
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: indexing.h,v 1.1.1.1 2006/08/12 00:22:10 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef INDEXING_H
#define INDEXING_H


/*
 * Some definitions for indices on pg_attribute
 */
#define Num_pg_aggregate_indices	1
#define Num_pg_am_indices			1
#define Num_pg_amop_indices			2
#define Num_pg_attr_indices			2
#define Num_pg_attrdef_indices		1
#define Num_pg_class_indices		2
#define Num_pg_description_indices	1
#define Num_pg_extstore_indices		1
#define Num_pg_group_indices		2
#define Num_pg_index_indices		1
#define Num_pg_inherits_indices		1
#define Num_pg_language_indices		2
#define Num_pg_listener_indices		1
#define Num_pg_opclass_indices		2
#define Num_pg_operator_indices		2
#define Num_pg_proc_indices			2
#define Num_pg_relcheck_indices		1
#define Num_pg_rewrite_indices		2
#define Num_pg_shadow_indices		2
#define Num_pg_statistic_indices	1
#define Num_pg_trigger_indices		3
#define Num_pg_type_indices			2


/*
 * Names of indices on system catalogs
 */
#define AccessMethodOpidIndex		"pg_amop_opid_index"
#define AccessMethodStrategyIndex	"pg_amop_strategy_index"
#define AggregateNameTypeIndex		"pg_aggregate_name_type_index"
#define AmNameIndex					"pg_am_name_index"
#define AttrDefaultIndex			"pg_attrdef_adrelid_index"
#define AttributeRelidNameIndex		"pg_attribute_relid_attnam_index"
#define AttributeRelidNumIndex		"pg_attribute_relid_attnum_index"
#define ClassNameIndex				"pg_class_relname_index"
#define ClassOidIndex				"pg_class_oid_index"
#define DescriptionObjIndex			"pg_description_objoid_index"
#define ExtStoreAttnumIndex                   "pg_extstore_relid_attnum_index"
#define GroupNameIndex				"pg_group_name_index"
#define GroupSysidIndex				"pg_group_sysid_index"
#define IndexRelidIndex				"pg_index_indexrelid_index"
#define InheritsRelidSeqnoIndex		"pg_inherits_relid_seqno_index"
#define LanguageNameIndex			"pg_language_name_index"
#define LanguageOidIndex			"pg_language_oid_index"
#define ListenerRelnamePidIndex		"pg_listener_relname_pid_index"
#define OpclassDeftypeIndex			"pg_opclass_deftype_index"
#define OpclassNameIndex			"pg_opclass_name_index"
#define OperatorNameIndex			"pg_operator_oprname_l_r_k_index"
#define OperatorOidIndex			"pg_operator_oid_index"
#define ProcedureNameIndex			"pg_proc_proname_narg_type_index"
#define ProcedureOidIndex			"pg_proc_oid_index"
#define RelCheckIndex				"pg_relcheck_rcrelid_index"
#define RewriteOidIndex				"pg_rewrite_oid_index"
#define RewriteRulenameIndex		"pg_rewrite_rulename_index"
#define ShadowNameIndex				"pg_shadow_name_index"
#define ShadowSysidIndex			"pg_shadow_sysid_index"
#define StatisticRelidAttnumIndex	"pg_statistic_relid_att_index"
#define TriggerConstrNameIndex		"pg_trigger_tgconstrname_index"
#define TriggerConstrRelidIndex		"pg_trigger_tgconstrrelid_index"
#define TriggerRelidIndex			"pg_trigger_tgrelid_index"
#define TypeNameIndex				"pg_type_typname_index"
#define TypeOidIndex				"pg_type_oid_index"

extern char *Name_pg_aggregate_indices[];
extern char *Name_pg_am_indices[];
extern char *Name_pg_amop_indices[];
extern char *Name_pg_attr_indices[];
extern char *Name_pg_attrdef_indices[];
extern char *Name_pg_class_indices[];
extern char *Name_pg_description_indices[];
extern char *Name_pg_extstore_indices[];
extern char *Name_pg_group_indices[];
extern char *Name_pg_index_indices[];
extern char *Name_pg_inherits_indices[];
extern char *Name_pg_language_indices[];
extern char *Name_pg_listener_indices[];
extern char *Name_pg_opclass_indices[];
extern char *Name_pg_operator_indices[];
extern char *Name_pg_proc_indices[];
extern char *Name_pg_relcheck_indices[];
extern char *Name_pg_rewrite_indices[];
extern char *Name_pg_shadow_indices[];
extern char *Name_pg_statistic_indices[];
extern char *Name_pg_trigger_indices[];
extern char *Name_pg_type_indices[];


extern char *IndexedCatalogNames[];


/*
 * indexing.c prototypes
 *
 * Functions for each index to perform the necessary scan on a cache miss.
 */
PG_EXTERN void CatalogOpenIndices(int nIndices, char **names, Relation *idescs);
PG_EXTERN void CatalogCloseIndices(int nIndices, Relation *idescs);
PG_EXTERN void CatalogIndexInsert(Relation *idescs,
				   int nIndices,
				   Relation heapRelation,
				   HeapTuple heapTuple);
PG_EXTERN bool CatalogHasIndex(char *catName, Oid catId);

PG_EXTERN HeapTuple AccessMethodOpidIndexScan(Relation heapRelation,
						  Oid claid, Oid opopr, Oid opid);
PG_EXTERN HeapTuple AccessMethodStrategyIndexScan(Relation heapRelation,
							  Oid opid, Oid claid, int2 opstrategy);
PG_EXTERN HeapTuple AggregateNameTypeIndexScan(Relation heapRelation,
						   char *aggName, Oid aggType);
PG_EXTERN HeapTuple AmNameIndexScan(Relation heapRelation, char *amName);
PG_EXTERN HeapTuple AttributeRelidNameIndexScan(Relation heapRelation,
							Oid relid, char *attname);
PG_EXTERN HeapTuple AttributeRelidNumIndexScan(Relation heapRelation,
						   Oid relid, AttrNumber attnum);
PG_EXTERN HeapTuple ClassNameIndexScan(Relation heapRelation, char *relName);
PG_EXTERN HeapTuple ClassNameIndexScan(Relation heapRelation, char *relName);
PG_EXTERN HeapTuple ClassOidIndexScan(Relation heapRelation, Oid relId);
PG_EXTERN HeapTuple ExtStoreAttnumIndexScan(Relation heapRelation, Oid relid,
							int4 attnum);
PG_EXTERN HeapTuple GroupNameIndexScan(Relation heapRelation, char *groName);
PG_EXTERN HeapTuple GroupSysidIndexScan(Relation heapRelation, int4 sysId);
extern HeapTuple IndexRelidIndexScan(Relation heapRelation, Oid relid);
PG_EXTERN HeapTuple InheritsRelidSeqnoIndexScan(Relation heapRelation, Oid relid,
							int4 seqno);
PG_EXTERN HeapTuple LanguageNameIndexScan(Relation heapRelation, char *lanName);
PG_EXTERN HeapTuple LanguageOidIndexScan(Relation heapRelation, Oid lanId);
PG_EXTERN HeapTuple ListenerRelnamePidIndexScan(Relation heapRelation, char *relName, int4 pid);
PG_EXTERN HeapTuple OpclassDeftypeIndexScan(Relation heapRelation, Oid defType);
PG_EXTERN HeapTuple OpclassNameIndexScan(Relation heapRelation, char *opcName);
PG_EXTERN HeapTuple OperatorNameIndexScan(Relation heapRelation,
				 char *oprName, Oid oprLeft, Oid oprRight, char oprKind);
PG_EXTERN HeapTuple OperatorOidIndexScan(Relation heapRelation, Oid oprId);
PG_EXTERN HeapTuple ProcedureNameIndexScan(Relation heapRelation,
					   char *procName, int2 nargs, Oid *argTypes);
PG_EXTERN HeapTuple ProcedureOidIndexScan(Relation heapRelation, Oid procId);
PG_EXTERN HeapTuple RewriteOidIndexScan(Relation heapRelation, Oid rewriteId);
PG_EXTERN HeapTuple RewriteRulenameIndexScan(Relation heapRelation,
						 char *ruleName);
PG_EXTERN HeapTuple ShadowNameIndexScan(Relation heapRelation, char *useName);
PG_EXTERN HeapTuple ShadowSysidIndexScan(Relation heapRelation, int4 sysId);
PG_EXTERN HeapTuple StatisticRelidAttnumIndexScan(Relation heapRelation,
							  Oid relId, AttrNumber attNum);
PG_EXTERN HeapTuple TypeNameIndexScan(Relation heapRelation, char *typeName);
PG_EXTERN HeapTuple TypeOidIndexScan(Relation heapRelation, Oid typeId);

/*
 * What follows are lines processed by genbki.sh to create the statements
 * the bootstrap parser will turn into DefineIndex commands.
 *
 * The keyword is DECLARE_INDEX every thing after that is just like in a
 * normal specification of the 'define index' POSTQUEL command.
 */

DECLARE_UNIQUE_INDEX(pg_aggregate_name_type_index on pg_aggregate using btree(aggname name_ops, aggbasetype oid_ops));
DECLARE_UNIQUE_INDEX(pg_am_name_index on pg_am using btree(amname name_ops));
DECLARE_UNIQUE_INDEX(pg_amop_opid_index on pg_amop using btree(amopclaid oid_ops, amopopr oid_ops, amopid oid_ops));
DECLARE_UNIQUE_INDEX(pg_amop_strategy_index on pg_amop using btree(amopid oid_ops, amopclaid oid_ops, amopstrategy int2_ops));
/* This following index is not used for a cache and is not unique */
DECLARE_INDEX(pg_attrdef_adrelid_index on pg_attrdef using btree(adrelid oid_ops));
DECLARE_UNIQUE_INDEX(pg_attribute_relid_attnam_index on pg_attribute using btree(attrelid oid_ops, attname name_ops));
DECLARE_UNIQUE_INDEX(pg_attribute_relid_attnum_index on pg_attribute using btree(attrelid oid_ops, attnum int2_ops));
DECLARE_UNIQUE_INDEX(pg_class_oid_index on pg_class using btree(oid oid_ops));
DECLARE_UNIQUE_INDEX(pg_class_relname_index on pg_class using btree(relname name_ops));
DECLARE_UNIQUE_INDEX(pg_description_objoid_index on pg_description using btree(objoid oid_ops));
DECLARE_UNIQUE_INDEX(pg_extstore_relid_attnum_index on pg_extstore using btree(extrelid oid_ops, extattnum int2_ops));
DECLARE_UNIQUE_INDEX(pg_group_name_index on pg_group using btree(groname name_ops));
DECLARE_UNIQUE_INDEX(pg_group_sysid_index on pg_group using btree(grosysid int4_ops));
DECLARE_UNIQUE_INDEX(pg_index_indexrelid_index on pg_index using btree(indexrelid oid_ops));
DECLARE_UNIQUE_INDEX(pg_inherits_relid_seqno_index on pg_inherits using btree(inhrelid oid_ops, inhseqno int4_ops));
DECLARE_UNIQUE_INDEX(pg_language_name_index on pg_language using btree(lanname name_ops));
DECLARE_UNIQUE_INDEX(pg_language_oid_index on pg_language using btree(oid oid_ops));
DECLARE_UNIQUE_INDEX(pg_listener_relname_pid_index on pg_listener using btree(relname name_ops, listenerpid int4_ops));
/* This column needs to allow multiple zero entries, but is in the cache */
DECLARE_INDEX(pg_opclass_deftype_index on pg_opclass using btree(opcdeftype oid_ops));
DECLARE_UNIQUE_INDEX(pg_opclass_name_index on pg_opclass using btree(opcname name_ops));
DECLARE_UNIQUE_INDEX(pg_operator_oid_index on pg_operator using btree(oid oid_ops));
DECLARE_UNIQUE_INDEX(pg_operator_oprname_l_r_k_index on pg_operator using btree(oprname name_ops, oprleft oid_ops, oprright oid_ops, oprkind char_ops));
DECLARE_UNIQUE_INDEX(pg_proc_oid_index on pg_proc using btree(oid oid_ops));
DECLARE_UNIQUE_INDEX(pg_proc_proname_narg_type_index on pg_proc using btree(proname name_ops, pronargs int2_ops, proargtypes oidvector_ops));
/* This following index is not used for a cache and is not unique */
DECLARE_INDEX(pg_relcheck_rcrelid_index on pg_relcheck using btree(rcrelid oid_ops));
DECLARE_UNIQUE_INDEX(pg_rewrite_oid_index on pg_rewrite using btree(oid oid_ops));
DECLARE_UNIQUE_INDEX(pg_rewrite_rulename_index on pg_rewrite using btree(rulename name_ops));
/*
xDECLARE_UNIQUE_INDEX(pg_shadow_name_index on pg_shadow using btree(usename name_ops));
xDECLARE_UNIQUE_INDEX(pg_shadow_sysid_index on pg_shadow using btree(usesysid int4_ops));
*/
DECLARE_INDEX(pg_statistic_relid_att_index on pg_statistic using btree(starelid oid_ops, staattnum int2_ops));
DECLARE_INDEX(pg_trigger_tgconstrname_index on pg_trigger using btree(tgconstrname name_ops));
DECLARE_INDEX(pg_trigger_tgconstrrelid_index on pg_trigger using btree(tgconstrrelid oid_ops));
DECLARE_INDEX(pg_trigger_tgrelid_index on pg_trigger using btree(tgrelid oid_ops));
DECLARE_UNIQUE_INDEX(pg_type_oid_index on pg_type using btree(oid oid_ops));
DECLARE_UNIQUE_INDEX(pg_type_typname_index on pg_type using btree(typname name_ops));

DECLARE_UNIQUE_INDEX(pg_extent_index on pg_extent using btree(relid oid_ops));

/* now build indices in the initialization scripts */
BUILD_INDICES

#endif	 /* INDEXING_H */
