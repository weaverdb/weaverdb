/*-------------------------------------------------------------------------
 *
 * builtins.h
 *	  Declarations for operations on built-in types.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: builtins.h,v 1.1.1.1 2006/08/12 00:22:26 synmscott Exp $
 *
 * NOTES
 *	  This should normally only be included by fmgr.h.
 *	  Under no circumstances should it ever be included before
 *	  including fmgr.h!
 * fmgr.h does not seem to include this file, so don't know where this
 *	comment came from. Backend code must include this stuff explicitly
 *	as far as I can tell...
 * - thomas 1998-06-08
 *
 *-------------------------------------------------------------------------
 */
#ifndef BUILTINS_H
#define BUILTINS_H

#include "storage/itemptr.h"
#include "access/heapam.h"		/* for HeapTuple */
#include "nodes/relation.h"		/* for amcostestimate parameters */
#include "utils/array.h"
#include "utils/inet.h"
#include "utils/int8.h"
#include "utils/geo_decls.h"
#include "utils/numeric.h"
#include "utils/datetime.h"
#include "utils/timestamp.h"
#include "utils/nabstime.h"
#include "utils/date.h"
#include "utils/lztext.h"
#include "utils/varbit.h"

/*
 *		Defined in adt/
 */
 #ifdef __cplusplus
extern "C" {
 #endif 
 
/* bool.c */
PG_EXTERN bool boolin(char *b);
PG_EXTERN char *boolout(bool b);
PG_EXTERN bool booleq(bool arg1, bool arg2);
PG_EXTERN bool boolne(bool arg1, bool arg2);
PG_EXTERN bool boollt(bool arg1, bool arg2);
PG_EXTERN bool boolgt(bool arg1, bool arg2);
PG_EXTERN bool boolle(bool arg1, bool arg2);
PG_EXTERN bool boolge(bool arg1, bool arg2);
PG_EXTERN bool istrue(bool arg1);
PG_EXTERN bool isfalse(bool arg1);

/* char.c */
PG_EXTERN int32 charin(char *ch);
PG_EXTERN char *charout(int32 ch);
PG_EXTERN int32 cidin(char *s);
PG_EXTERN char *cidout(int32 c);
PG_EXTERN bool chareq(int8 arg1, int8 arg2);
PG_EXTERN bool charne(int8 arg1, int8 arg2);
PG_EXTERN bool charlt(int8 arg1, int8 arg2);
PG_EXTERN bool charle(int8 arg1, int8 arg2);
PG_EXTERN bool chargt(int8 arg1, int8 arg2);
PG_EXTERN bool charge(int8 arg1, int8 arg2);
PG_EXTERN int8 charpl(int8 arg1, int8 arg2);
PG_EXTERN int8 charmi(int8 arg1, int8 arg2);
PG_EXTERN int8 charmul(int8 arg1, int8 arg2);
PG_EXTERN int8 chardiv(int8 arg1, int8 arg2);
PG_EXTERN bool cideq(int8 arg1, int8 arg2);
PG_EXTERN int8 text_char(text *arg1);
PG_EXTERN text *char_text(int8 arg1);

/* int.c */
PG_EXTERN int32 int2in(char *num);
PG_EXTERN char *int2out(int16 sh);
PG_EXTERN int16 *int2vectorin(char *shs);
PG_EXTERN char *int2vectorout(int16 *shs);
PG_EXTERN bool int2vectoreq(int16 *arg1, int16 *arg2);
PG_EXTERN int32 *int44in(char *input_string);
PG_EXTERN char *int44out(int32 *an_array);
PG_EXTERN int32 int4in(char *num);
PG_EXTERN char *int4out(int32 l);
PG_EXTERN int32 i2toi4(int16 arg1);
PG_EXTERN int16 i4toi2(int32 arg1);
PG_EXTERN text *int2_text(int16 arg1);
PG_EXTERN int16 text_int2(text *arg1);
PG_EXTERN text *int4_text(int32 arg1);
PG_EXTERN int32 text_int4(text *arg1);
PG_EXTERN bool int4eq(int32 arg1, int32 arg2);
PG_EXTERN bool int4ne(int32 arg1, int32 arg2);
PG_EXTERN bool int4lt(int32 arg1, int32 arg2);
PG_EXTERN bool int4le(int32 arg1, int32 arg2);
PG_EXTERN bool int4gt(int32 arg1, int32 arg2);
PG_EXTERN bool int4ge(int32 arg1, int32 arg2);
PG_EXTERN bool int2eq(int16 arg1, int16 arg2);
PG_EXTERN bool int2ne(int16 arg1, int16 arg2);
PG_EXTERN bool int2lt(int16 arg1, int16 arg2);
PG_EXTERN bool int2le(int16 arg1, int16 arg2);
PG_EXTERN bool int2gt(int16 arg1, int16 arg2);
PG_EXTERN bool int2ge(int16 arg1, int16 arg2);
PG_EXTERN bool int24eq(int32 arg1, int32 arg2);
PG_EXTERN bool int24ne(int32 arg1, int32 arg2);
PG_EXTERN bool int24lt(int32 arg1, int32 arg2);
PG_EXTERN bool int24le(int32 arg1, int32 arg2);
PG_EXTERN bool int24gt(int32 arg1, int32 arg2);
PG_EXTERN bool int24ge(int32 arg1, int32 arg2);
PG_EXTERN bool int42eq(int32 arg1, int32 arg2);
PG_EXTERN bool int42ne(int32 arg1, int32 arg2);
PG_EXTERN bool int42lt(int32 arg1, int32 arg2);
PG_EXTERN bool int42le(int32 arg1, int32 arg2);
PG_EXTERN bool int42gt(int32 arg1, int32 arg2);
PG_EXTERN bool int42ge(int32 arg1, int32 arg2);
PG_EXTERN int32 int4um(int32 arg);
PG_EXTERN int32 int4pl(int32 arg1, int32 arg2);
PG_EXTERN int32 int4mi(int32 arg1, int32 arg2);
PG_EXTERN int32 int4mul(int32 arg1, int32 arg2);
PG_EXTERN int32 int4div(int32 arg1, int32 arg2);
PG_EXTERN int32 int4abs(int32 arg);
PG_EXTERN int32 int4inc(int32 arg);
PG_EXTERN int16 int2um(int16 arg);
PG_EXTERN int16 int2pl(int16 arg1, int16 arg2);
PG_EXTERN int16 int2mi(int16 arg1, int16 arg2);
PG_EXTERN int16 int2mul(int16 arg1, int16 arg2);
PG_EXTERN int16 int2div(int16 arg1, int16 arg2);
PG_EXTERN int16 int2abs(int16 arg);
PG_EXTERN int16 int2inc(int16 arg);
PG_EXTERN int32 int24pl(int32 arg1, int32 arg2);
PG_EXTERN int32 int24mi(int32 arg1, int32 arg2);
PG_EXTERN int32 int24mul(int32 arg1, int32 arg2);
PG_EXTERN int32 int24div(int32 arg1, int32 arg2);
PG_EXTERN int32 int42pl(int32 arg1, int32 arg2);
PG_EXTERN int32 int42mi(int32 arg1, int32 arg2);
PG_EXTERN int32 int42mul(int32 arg1, int32 arg2);
PG_EXTERN int32 int42div(int32 arg1, int32 arg2);
PG_EXTERN int32 int4mod(int32 arg1, int32 arg2);
PG_EXTERN int32 int2mod(int16 arg1, int16 arg2);
PG_EXTERN int32 int24mod(int32 arg1, int32 arg2);
PG_EXTERN int32 int42mod(int32 arg1, int32 arg2);
PG_EXTERN int32 int4fac(int32 arg1);
PG_EXTERN int32 int2fac(int16 arg1);
PG_EXTERN int16 int2larger(int16 arg1, int16 arg2);
PG_EXTERN int16 int2smaller(int16 arg1, int16 arg2);
PG_EXTERN int32 int4larger(int32 arg1, int32 arg2);
PG_EXTERN int32 int4smaller(int32 arg1, int32 arg2);

/* name.c */
PG_EXTERN NameData *namein(const char *s);
PG_EXTERN char *nameout(const NameData *s);
PG_EXTERN bool nameeq(const NameData *arg1, const NameData *arg2);
PG_EXTERN bool namene(const NameData *arg1, const NameData *arg2);
PG_EXTERN bool namelt(const NameData *arg1, const NameData *arg2);
PG_EXTERN bool namele(const NameData *arg1, const NameData *arg2);
PG_EXTERN bool namegt(const NameData *arg1, const NameData *arg2);
PG_EXTERN bool namege(const NameData *arg1, const NameData *arg2);
PG_EXTERN int	namecpy(Name n1, Name n2);
PG_EXTERN int	namestrcpy(Name name, const char *str);
PG_EXTERN int	namestrcmp(Name name, const char *str);

/* numutils.c */
/* XXX hack.  HP-UX has a ltoa (with different arguments) already. */
#ifdef __hpux
#define ltoa pg_ltoa
#endif	 /* hpux */
PG_EXTERN int32 pg_atoi(char *s, int size, int c);

/* XXX hack.  QNX has itoa and ltoa (with different arguments) already. */
#ifdef __QNX__
#define itoa pg_itoa
#define ltoa pg_ltoa
#endif	 /* QNX */
PG_EXTERN void itoa(int i, char *a);
PG_EXTERN void ltoa(int32 l, char *a);
PG_EXTERN void lltoa(int64 l, char *a);

/*
 *		Per-opclass comparison functions for new btrees.  These are
 *		stored in pg_amproc and defined in nbtree/
 */
PG_EXTERN int32 btint2cmp(int16 a, int16 b);
PG_EXTERN int32 btint4cmp(int32 a, int32 b);
PG_EXTERN int32 btint8cmp(int64 *a, int64 *b);
PG_EXTERN int32 btint24cmp(int16 a, int32 b);
PG_EXTERN int32 btint42cmp(int32 a, int16 b);
PG_EXTERN int32 btfloat4cmp(float32 a, float32 b);
PG_EXTERN int32 btfloat8cmp(float64 a, float64 b);
PG_EXTERN int32 btoidcmp(Oid a, Oid b);
PG_EXTERN int32 btoidvectorcmp(Oid *a, Oid *b);
PG_EXTERN int32 btabstimecmp(AbsoluteTime a, AbsoluteTime b);
PG_EXTERN int32 btcharcmp(char a, char b);
PG_EXTERN int32 btnamecmp(NameData *a, NameData *b);
PG_EXTERN int32 bttextcmp(struct varlena * a, struct varlena * b);
PG_EXTERN int32 btboolcmp(bool a, bool b);

/* support routines for the rtree access method, by opclass */
PG_EXTERN BOX *rt_rect_union(BOX *a, BOX *b);
PG_EXTERN BOX *rt_rect_inter(BOX *a, BOX *b);
PG_EXTERN void rt_rect_size(BOX *a, float *size);
PG_EXTERN void rt_bigbox_size(BOX *a, float *size);
PG_EXTERN void rt_poly_size(POLYGON *a, float *size);
PG_EXTERN POLYGON *rt_poly_union(POLYGON *a, POLYGON *b);
PG_EXTERN POLYGON *rt_poly_inter(POLYGON *a, POLYGON *b);


PG_EXTERN int32 pqtest(struct varlena * vlena);

/* arrayfuncs.c */

/* filename.c */
PG_EXTERN char *filename_in(char *file);
PG_EXTERN char *filename_out(char *s);

/* float.c */
PG_EXTERN void CheckFloat8Val(double val); /* used by lex */
PG_EXTERN float32 float4in(char *num);
PG_EXTERN char *float4out(float32 num);
PG_EXTERN float64 float8in(char *num);
PG_EXTERN char *float8out(float64 num);
PG_EXTERN float32 float4abs(float32 arg1);
PG_EXTERN float32 float4um(float32 arg1);
PG_EXTERN float32 float4larger(float32 arg1, float32 arg2);
PG_EXTERN float32 float4smaller(float32 arg1, float32 arg2);
PG_EXTERN float64 float8abs(float64 arg1);
PG_EXTERN float64 float8um(float64 arg1);
PG_EXTERN float64 float8larger(float64 arg1, float64 arg2);
PG_EXTERN float64 float8smaller(float64 arg1, float64 arg2);
PG_EXTERN float32 float4pl(float32 arg1, float32 arg2);
PG_EXTERN float32 float4mi(float32 arg1, float32 arg2);
PG_EXTERN float32 float4mul(float32 arg1, float32 arg2);
PG_EXTERN float32 float4div(float32 arg1, float32 arg2);
PG_EXTERN float32 float4inc(float32 arg1);
PG_EXTERN float64 float8pl(float64 arg1, float64 arg2);
PG_EXTERN float64 float8mi(float64 arg1, float64 arg2);
PG_EXTERN float64 float8mul(float64 arg1, float64 arg2);
PG_EXTERN float64 float8div(float64 arg1, float64 arg2);
PG_EXTERN float64 float8inc(float64 arg1);
PG_EXTERN bool float4eq(float32 arg1, float32 arg2);
PG_EXTERN bool float4ne(float32 arg1, float32 arg2);
PG_EXTERN bool float4lt(float32 arg1, float32 arg2);
PG_EXTERN bool float4le(float32 arg1, float32 arg2);
PG_EXTERN bool float4gt(float32 arg1, float32 arg2);
PG_EXTERN bool float4ge(float32 arg1, float32 arg2);
PG_EXTERN bool float8eq(float64 arg1, float64 arg2);
PG_EXTERN bool float8ne(float64 arg1, float64 arg2);
PG_EXTERN bool float8lt(float64 arg1, float64 arg2);
PG_EXTERN bool float8le(float64 arg1, float64 arg2);
PG_EXTERN bool float8gt(float64 arg1, float64 arg2);
PG_EXTERN bool float8ge(float64 arg1, float64 arg2);
PG_EXTERN float64 ftod(float32 num);
PG_EXTERN float64 i4tod(int32 num);
PG_EXTERN float64 i2tod(int16 num);
PG_EXTERN float32 dtof(float64 num);
PG_EXTERN int32 dtoi4(float64 num);
PG_EXTERN int16 dtoi2(float64 num);
PG_EXTERN float32 i4tof(int32 num);
PG_EXTERN float32 i2tof(int16 num);
PG_EXTERN int32 ftoi4(float32 num);
PG_EXTERN int16 ftoi2(float32 num);
PG_EXTERN float64 text_float8(text *str);
PG_EXTERN float32 text_float4(text *str);
PG_EXTERN text *float8_text(float64 num);
PG_EXTERN text *float4_text(float32 num);
PG_EXTERN float64 dround(float64 arg1);
PG_EXTERN float64 dtrunc(float64 arg1);
PG_EXTERN float64 dsqrt(float64 arg1);
PG_EXTERN float64 dcbrt(float64 arg1);
PG_EXTERN float64 dpow(float64 arg1, float64 arg2);
PG_EXTERN float64 dexp(float64 arg1);
PG_EXTERN float64 dlog1(float64 arg1);
PG_EXTERN float64 dlog10(float64 arg1);
PG_EXTERN float64 dacos(float64 arg1);
PG_EXTERN float64 dasin(float64 arg1);
PG_EXTERN float64 datan(float64 arg1);
PG_EXTERN float64 datan2(float64 arg1, float64 arg2);
PG_EXTERN float64 dcos(float64 arg1);
PG_EXTERN float64 dcot(float64 arg1);
PG_EXTERN float64 dsin(float64 arg1);
PG_EXTERN float64 dtan(float64 arg1);
PG_EXTERN float64 degrees(float64 arg1);
PG_EXTERN float64 dpi(void);
PG_EXTERN float64 radians(float64 arg1);
PG_EXTERN float64 dtan(float64 arg1);
PG_EXTERN float64 drandom(void);
PG_EXTERN int32 setseed(float64 seed);

PG_EXTERN float64 float48pl(float32 arg1, float64 arg2);
PG_EXTERN float64 float48mi(float32 arg1, float64 arg2);
PG_EXTERN float64 float48mul(float32 arg1, float64 arg2);
PG_EXTERN float64 float48div(float32 arg1, float64 arg2);
PG_EXTERN float64 float84pl(float64 arg1, float32 arg2);
PG_EXTERN float64 float84mi(float64 arg1, float32 arg2);
PG_EXTERN float64 float84mul(float64 arg1, float32 arg2);
PG_EXTERN float64 float84div(float64 arg1, float32 arg2);
PG_EXTERN bool float48eq(float32 arg1, float64 arg2);
PG_EXTERN bool float48ne(float32 arg1, float64 arg2);
PG_EXTERN bool float48lt(float32 arg1, float64 arg2);
PG_EXTERN bool float48le(float32 arg1, float64 arg2);
PG_EXTERN bool float48gt(float32 arg1, float64 arg2);
PG_EXTERN bool float48ge(float32 arg1, float64 arg2);
PG_EXTERN bool float84eq(float64 arg1, float32 arg2);
PG_EXTERN bool float84ne(float64 arg1, float32 arg2);
PG_EXTERN bool float84lt(float64 arg1, float32 arg2);
PG_EXTERN bool float84le(float64 arg1, float32 arg2);
PG_EXTERN bool float84gt(float64 arg1, float32 arg2);
PG_EXTERN bool float84ge(float64 arg1, float32 arg2);

/* misc.c */
PG_EXTERN bool nullvalue(Datum value, bool *isNull);
PG_EXTERN bool nonnullvalue(Datum value, bool *isNull);
PG_EXTERN bool oidrand(Oid o, int32 X);
PG_EXTERN bool oidsrand(int32 X);
PG_EXTERN int32 userfntest(int i);

/* define macros to replace mixed-case function calls - tgl 97/04/27 */
#define NullValue(v,b) nullvalue(v,b)
#define NonNullValue(v,b) nonnullvalue(v,b)

/* not_in.c */
PG_EXTERN bool int4notin(int32 not_in_arg, char *relation_and_attr);
PG_EXTERN bool oidnotin(Oid the_oid, char *compare);

/* oid.c */
PG_EXTERN Oid *oidvectorin(char *oidString);
PG_EXTERN char *oidvectorout(Oid *oidArray);
PG_EXTERN Oid	oidin(char *s);
PG_EXTERN char *oidout(Oid o);
PG_EXTERN bool oideq(Oid arg1, Oid arg2);
PG_EXTERN bool oidne(Oid arg1, Oid arg2);
PG_EXTERN bool oidvectoreq(Oid *arg1, Oid *arg2);
PG_EXTERN bool oidvectorne(Oid *arg1, Oid *arg2);
PG_EXTERN bool oidvectorlt(Oid *arg1, Oid *arg2);
PG_EXTERN bool oidvectorle(Oid *arg1, Oid *arg2);
PG_EXTERN bool oidvectorge(Oid *arg1, Oid *arg2);
PG_EXTERN bool oidvectorgt(Oid *arg1, Oid *arg2);
PG_EXTERN bool oideqint4(Oid arg1, int32 arg2);
PG_EXTERN bool int4eqoid(int32 arg1, Oid arg2);
PG_EXTERN text *oid_text(Oid arg1);
PG_EXTERN Oid	text_oid(text *arg1);
PG_EXTERN long longin(char* s);
PG_EXTERN char* longout(long l);
PG_EXTERN bool longeqoid(long arg1,Oid arg2);
PG_EXTERN bool oideqlong(Oid arg1,long arg2);

/* regexp.c */
PG_EXTERN bool nameregexeq(NameData *n, struct varlena * p);
PG_EXTERN bool nameregexne(NameData *s, struct varlena * p);
PG_EXTERN bool textregexeq(struct varlena * s, struct varlena * p);
PG_EXTERN bool textregexne(struct varlena * s, struct varlena * p);
PG_EXTERN bool nameicregexeq(NameData *s, struct varlena * p);
PG_EXTERN bool nameicregexne(NameData *s, struct varlena * p);
PG_EXTERN bool texticregexeq(struct varlena * s, struct varlena * p);
PG_EXTERN bool texticregexne(struct varlena * s, struct varlena * p);


/* regproc.c */
PG_EXTERN RegProcedure regprocin(char *pro_name_and_oid);
PG_EXTERN char *regprocout(RegProcedure proid);
PG_EXTERN text *oidvectortypes(Oid *oidArray);
PG_EXTERN Oid	regproctooid(RegProcedure rp);

/* define macro to replace mixed-case function call - tgl 97/04/27 */
#define RegprocToOid(rp) regproctooid(rp)

/* ruleutils.c */
PG_EXTERN text *pg_get_ruledef(NameData *rname);
PG_EXTERN text *pg_get_viewdef(NameData *rname);
PG_EXTERN text *pg_get_indexdef(Oid indexrelid);
PG_EXTERN NameData *pg_get_userbyid(int32 uid);
PG_EXTERN char *deparse_expression(Node *expr, List *rangetables,
				   bool forceprefix);

/* selfuncs.c */
PG_EXTERN float64 eqsel(Oid opid, Oid relid, AttrNumber attno,
					 Datum value, int32 flag);
PG_EXTERN float64 neqsel(Oid opid, Oid relid, AttrNumber attno,
					  Datum value, int32 flag);
PG_EXTERN float64 scalarltsel(Oid opid, Oid relid, AttrNumber attno,
						   Datum value, int32 flag);
PG_EXTERN float64 scalargtsel(Oid opid, Oid relid, AttrNumber attno,
						   Datum value, int32 flag);
PG_EXTERN float64 regexeqsel(Oid opid, Oid relid, AttrNumber attno,
						  Datum value, int32 flag);
PG_EXTERN float64 likesel(Oid opid, Oid relid, AttrNumber attno,
					   Datum value, int32 flag);
PG_EXTERN float64 icregexeqsel(Oid opid, Oid relid, AttrNumber attno,
							Datum value, int32 flag);
PG_EXTERN float64 regexnesel(Oid opid, Oid relid, AttrNumber attno,
						  Datum value, int32 flag);
PG_EXTERN float64 nlikesel(Oid opid, Oid relid, AttrNumber attno,
						Datum value, int32 flag);
PG_EXTERN float64 icregexnesel(Oid opid, Oid relid, AttrNumber attno,
							Datum value, int32 flag);

PG_EXTERN float64 eqjoinsel(Oid opid, Oid relid1, AttrNumber attno1,
						 Oid relid2, AttrNumber attno2);
PG_EXTERN float64 neqjoinsel(Oid opid, Oid relid1, AttrNumber attno1,
						  Oid relid2, AttrNumber attno2);
PG_EXTERN float64 scalarltjoinsel(Oid opid, Oid relid1, AttrNumber attno1,
							   Oid relid2, AttrNumber attno2);
PG_EXTERN float64 scalargtjoinsel(Oid opid, Oid relid1, AttrNumber attno1,
							   Oid relid2, AttrNumber attno2);
PG_EXTERN float64 regexeqjoinsel(Oid opid, Oid relid1, AttrNumber attno1,
							  Oid relid2, AttrNumber attno2);
PG_EXTERN float64 likejoinsel(Oid opid, Oid relid1, AttrNumber attno1,
						   Oid relid2, AttrNumber attno2);
PG_EXTERN float64 icregexeqjoinsel(Oid opid, Oid relid1, AttrNumber attno1,
								Oid relid2, AttrNumber attno2);
PG_EXTERN float64 regexnejoinsel(Oid opid, Oid relid1, AttrNumber attno1,
							  Oid relid2, AttrNumber attno2);
PG_EXTERN float64 nlikejoinsel(Oid opid, Oid relid1, AttrNumber attno1,
							Oid relid2, AttrNumber attno2);
PG_EXTERN float64 icregexnejoinsel(Oid opid, Oid relid1, AttrNumber attno1,
								Oid relid2, AttrNumber attno2);

PG_EXTERN void btcostestimate(Query *root, RelOptInfo *rel,
			   IndexOptInfo *index, List *indexQuals,
			   Cost *indexStartupCost,
			   Cost *indexTotalCost,
			   Selectivity *indexSelectivity);
PG_EXTERN void rtcostestimate(Query *root, RelOptInfo *rel,
			   IndexOptInfo *index, List *indexQuals,
			   Cost *indexStartupCost,
			   Cost *indexTotalCost,
			   Selectivity *indexSelectivity);
PG_EXTERN void hashcostestimate(Query *root, RelOptInfo *rel,
				 IndexOptInfo *index, List *indexQuals,
				 Cost *indexStartupCost,
				 Cost *indexTotalCost,
				 Selectivity *indexSelectivity);
PG_EXTERN void gistcostestimate(Query *root, RelOptInfo *rel,
				 IndexOptInfo *index, List *indexQuals,
				 Cost *indexStartupCost,
				 Cost *indexTotalCost,
				 Selectivity *indexSelectivity);

typedef enum
{
	Pattern_Type_Like, Pattern_Type_Regex, Pattern_Type_Regex_IC
} Pattern_Type;

typedef enum
{
	Pattern_Prefix_None, Pattern_Prefix_Partial, Pattern_Prefix_Exact
} Pattern_Prefix_Status;

PG_EXTERN Pattern_Prefix_Status pattern_fixed_prefix(char *patt,
												  Pattern_Type ptype,
												  char **prefix,
												  char **rest);
PG_EXTERN char *make_greater_string(const char *str, Oid datatype);

/* tid.c */
PG_EXTERN ItemPointer tidin(const char *str);
PG_EXTERN char *tidout(ItemPointer itemPtr);
PG_EXTERN bool tideq(ItemPointer, ItemPointer);
PG_EXTERN bool tidne(ItemPointer, ItemPointer);
PG_EXTERN text *tid_text(ItemPointer);
PG_EXTERN ItemPointer text_tid(const text *);
PG_EXTERN ItemPointer currtid_byreloid(Oid relOid, ItemPointer);
PG_EXTERN ItemPointer currtid_byrelname(const text *relName, ItemPointer);
PG_EXTERN bytea* tidtobytes(ItemPointer item);
PG_EXTERN ItemPointer bytestotid(bytea* bytes);

/* varchar.c */
PG_EXTERN char *bpcharin(char *s, int dummy, int32 atttypmod);
PG_EXTERN char *bpcharout(char *s);
PG_EXTERN char *bpchar(char *s, int32 slen);
PG_EXTERN ArrayType *_bpchar(ArrayType *v, int32 slen);
PG_EXTERN char *char_bpchar(int32 c);
PG_EXTERN int32 bpchar_char(char *s);
PG_EXTERN char *name_bpchar(NameData *s);
PG_EXTERN NameData *bpchar_name(char *s);
PG_EXTERN bool bpchareq(char *arg1, char *arg2);
PG_EXTERN bool bpcharne(char *arg1, char *arg2);
PG_EXTERN bool bpcharlt(char *arg1, char *arg2);
PG_EXTERN bool bpcharle(char *arg1, char *arg2);
PG_EXTERN bool bpchargt(char *arg1, char *arg2);
PG_EXTERN bool bpcharge(char *arg1, char *arg2);
PG_EXTERN int32 bpcharcmp(char *arg1, char *arg2);
PG_EXTERN int32 bpcharlen(char *arg);
PG_EXTERN int32 bpcharoctetlen(char *arg);
PG_EXTERN uint32 hashbpchar(struct varlena * key);

PG_EXTERN char *varcharin(char *s, int dummy, int32 atttypmod);
PG_EXTERN char *varcharout(char *s);
PG_EXTERN char *varchar(char *s, int32 slen);
PG_EXTERN ArrayType *_varchar(ArrayType *v, int32 slen);
PG_EXTERN bool byteaeq(bytea *arg1, bytea *arg2);
PG_EXTERN bool byteane(bytea *arg1, bytea *arg2);
PG_EXTERN bool bytealt(bytea *arg1, bytea *arg2);
PG_EXTERN bool byteale(bytea *arg1, bytea *arg2);
PG_EXTERN bool byteagt(bytea *arg1, bytea *arg2);
PG_EXTERN bool byteage(bytea *arg1, bytea *arg2);
PG_EXTERN bool varchareq(char *arg1, char *arg2);
PG_EXTERN bool varcharne(char *arg1, char *arg2);
PG_EXTERN bool varcharlt(char *arg1, char *arg2);
PG_EXTERN bool varcharle(char *arg1, char *arg2);
PG_EXTERN bool varchargt(char *arg1, char *arg2);
PG_EXTERN bool varcharge(char *arg1, char *arg2);
PG_EXTERN int32 varcharcmp(char *arg1, char *arg2);
PG_EXTERN int32 varcharlen(char *arg);
PG_EXTERN int32 varcharoctetlen(char *arg);
PG_EXTERN uint32 hashvarchar(struct varlena * key);
PG_EXTERN bytea* md5(struct varlena* src);

/* varlena.c */
PG_EXTERN text *textin(char *inputText);
PG_EXTERN char *textout(text *vlena);
PG_EXTERN text *textcat(text *arg1, text *arg2);
PG_EXTERN bool texteq(text *arg1, text *arg2);
PG_EXTERN bool textne(text *arg1, text *arg2);
PG_EXTERN int	varstr_cmp(char *arg1, int len1, char *arg2, int len2);
PG_EXTERN bool text_lt(text *arg1, text *arg2);
PG_EXTERN bool text_le(text *arg1, text *arg2);
PG_EXTERN bool text_gt(text *arg1, text *arg2);
PG_EXTERN bool text_ge(text *arg1, text *arg2);
PG_EXTERN text *text_larger(text *arg1, text *arg2);
PG_EXTERN text *text_smaller(text *arg1, text *arg2);
PG_EXTERN int32 textlen(text *arg);
PG_EXTERN int32 textoctetlen(text *arg);
PG_EXTERN int32 textpos(text *arg1, text *arg2);
PG_EXTERN text *text_substr(text *string, int32 m, int32 n);
PG_EXTERN text *name_text(NameData *s);
PG_EXTERN NameData *text_name(text *s);
PG_EXTERN int32 pagesize();

PG_EXTERN bytea *byteain(char *inputText);
PG_EXTERN char *byteaout(bytea *vlena);
PG_EXTERN int32 byteaoctetlen(bytea *v);
PG_EXTERN int32 byteaGetByte(bytea *v, int32 n);
PG_EXTERN int32 byteaGetBit(bytea *v, int32 n);
PG_EXTERN bytea *byteaSetByte(bytea *v, int32 n, int32 newByte);
PG_EXTERN bytea *byteaSetBit(bytea *v, int32 n, int32 newBit);

/* like.c */
PG_EXTERN bool namelike(NameData *n, struct varlena * p);
PG_EXTERN bool namenlike(NameData *s, struct varlena * p);
PG_EXTERN bool textlike(struct varlena * s, struct varlena * p);
PG_EXTERN bool textnlike(struct varlena * s, struct varlena * p);

/* oracle_compat.c */
#ifdef NOTUSED
PG_EXTERN text *lower(text *string);
PG_EXTERN text *upper(text *string);
PG_EXTERN text *initcap(text *string);
PG_EXTERN text *lpad(text *string1, int4 len, text *string2);
PG_EXTERN text *rpad(text *string1, int4 len, text *string2);
PG_EXTERN text *btrim(text *string, text *set);
PG_EXTERN text *ltrim(text *string, text *set);
PG_EXTERN text *rtrim(text *string, text *set);
PG_EXTERN text *substr(text *string, int4 m, int4 n);
PG_EXTERN text *translate(text *string, text *from, text *to);
PG_EXTERN text *ichar(int4 arg1);
PG_EXTERN text *repeat(text *string, int4 count);
PG_EXTERN int4 ascii(text *string);
#endif
/* acl.c */

/* inet_net_ntop.c */
PG_EXTERN char *inet_net_ntop(int af, const void *src, int bits, char *dst, size_t size);
PG_EXTERN char *inet_cidr_ntop(int af, const void *src, int bits, char *dst, size_t size);

/* inet_net_pton.c */
PG_EXTERN int	inet_net_pton(int af, const char *src, void *dst, size_t size);

/* network.c */
PG_EXTERN inet *inet_in(char *str);
PG_EXTERN char *inet_out(inet *addr);
PG_EXTERN inet *cidr_in(char *str);
PG_EXTERN char *cidr_out(inet *addr);
PG_EXTERN bool network_lt(inet *a1, inet *a2);
PG_EXTERN bool network_le(inet *a1, inet *a2);
PG_EXTERN bool network_eq(inet *a1, inet *a2);
PG_EXTERN bool network_ge(inet *a1, inet *a2);
PG_EXTERN bool network_gt(inet *a1, inet *a2);
PG_EXTERN bool network_ne(inet *a1, inet *a2);
PG_EXTERN bool network_sub(inet *a1, inet *a2);
PG_EXTERN bool network_subeq(inet *a1, inet *a2);
PG_EXTERN bool network_sup(inet *a1, inet *a2);
PG_EXTERN bool network_supeq(inet *a1, inet *a2);
PG_EXTERN int4 network_cmp(inet *a1, inet *a2);

PG_EXTERN text *network_network(inet *addr);
PG_EXTERN text *network_netmask(inet *addr);
PG_EXTERN int4 network_masklen(inet *addr);
PG_EXTERN text *network_broadcast(inet *addr);
PG_EXTERN text *network_host(inet *addr);

/* mac.c */
PG_EXTERN macaddr *macaddr_in(char *str);
PG_EXTERN char *macaddr_out(macaddr *addr);
PG_EXTERN bool macaddr_lt(macaddr *a1, macaddr *a2);
PG_EXTERN bool macaddr_le(macaddr *a1, macaddr *a2);
PG_EXTERN bool macaddr_eq(macaddr *a1, macaddr *a2);
PG_EXTERN bool macaddr_ge(macaddr *a1, macaddr *a2);
PG_EXTERN bool macaddr_gt(macaddr *a1, macaddr *a2);
PG_EXTERN bool macaddr_ne(macaddr *a1, macaddr *a2);
PG_EXTERN int4 macaddr_cmp(macaddr *a1, macaddr *a2);
PG_EXTERN text *macaddr_manuf(macaddr *addr);

/* numeric.c */
PG_EXTERN Numeric numeric_in(char *str, int dummy, int32 typmod);
PG_EXTERN char *numeric_out(Numeric num);
PG_EXTERN Numeric numeric(Numeric num, int32 typmod);
PG_EXTERN Numeric numeric_abs(Numeric num);
PG_EXTERN Numeric numeric_uminus(Numeric num);
PG_EXTERN Numeric numeric_sign(Numeric num);
PG_EXTERN Numeric numeric_round(Numeric num, int32 scale);
PG_EXTERN Numeric numeric_trunc(Numeric num, int32 scale);
PG_EXTERN Numeric numeric_ceil(Numeric num);
PG_EXTERN Numeric numeric_floor(Numeric num);
PG_EXTERN int32 numeric_cmp(Numeric num1, Numeric num2);
PG_EXTERN bool numeric_eq(Numeric num1, Numeric num2);
PG_EXTERN bool numeric_ne(Numeric num1, Numeric num2);
PG_EXTERN bool numeric_gt(Numeric num1, Numeric num2);
PG_EXTERN bool numeric_ge(Numeric num1, Numeric num2);
PG_EXTERN bool numeric_lt(Numeric num1, Numeric num2);
PG_EXTERN bool numeric_le(Numeric num1, Numeric num2);
PG_EXTERN Numeric numeric_add(Numeric num1, Numeric num2);
PG_EXTERN Numeric numeric_sub(Numeric num1, Numeric num2);
PG_EXTERN Numeric numeric_mul(Numeric num1, Numeric num2);
PG_EXTERN Numeric numeric_div(Numeric num1, Numeric num2);
PG_EXTERN Numeric numeric_mod(Numeric num1, Numeric num2);
PG_EXTERN Numeric numeric_inc(Numeric num);
PG_EXTERN Numeric numeric_dec(Numeric num);
PG_EXTERN Numeric numeric_smaller(Numeric num1, Numeric num2);
PG_EXTERN Numeric numeric_larger(Numeric num1, Numeric num2);
PG_EXTERN Numeric numeric_sqrt(Numeric num);
PG_EXTERN Numeric numeric_exp(Numeric num);
PG_EXTERN Numeric numeric_ln(Numeric num);
PG_EXTERN Numeric numeric_log(Numeric num1, Numeric num2);
PG_EXTERN Numeric numeric_power(Numeric num1, Numeric num2);
PG_EXTERN Numeric int4_numeric(int32 val);
PG_EXTERN int32 numeric_int4(Numeric num);
PG_EXTERN Numeric int8_numeric(int64 *val);
PG_EXTERN int64 *numeric_int8(Numeric num);
PG_EXTERN Numeric int2_numeric(int16 val);
PG_EXTERN int16 numeric_int2(Numeric num);
PG_EXTERN Numeric float4_numeric(float32 val);
PG_EXTERN float32 numeric_float4(Numeric num);
PG_EXTERN Numeric float8_numeric(float64 val);
PG_EXTERN float64 numeric_float8(Numeric num);

/* lztext.c */
lztext	   *lztextin(char *str);
char	   *lztextout(lztext *lz);
text	   *lztext_text(lztext *lz);
lztext	   *text_lztext(text *txt);
int32		lztextlen(lztext *lz);
int32		lztextoctetlen(lztext *lz);
int32		lztext_cmp(lztext *lz1, lztext *lz2);
bool		lztext_eq(lztext *lz1, lztext *lz2);
bool		lztext_ne(lztext *lz1, lztext *lz2);
bool		lztext_gt(lztext *lz1, lztext *lz2);
bool		lztext_ge(lztext *lz1, lztext *lz2);
bool		lztext_lt(lztext *lz1, lztext *lz2);
bool		lztext_le(lztext *lz1, lztext *lz2);

/* ri_triggers.c */
PG_EXTERN HeapTuple RI_FKey_check_ins(FmgrInfo *proinfo);
PG_EXTERN HeapTuple RI_FKey_check_upd(FmgrInfo *proinfo);
PG_EXTERN HeapTuple RI_FKey_noaction_del(FmgrInfo *proinfo);
PG_EXTERN HeapTuple RI_FKey_noaction_upd(FmgrInfo *proinfo);
PG_EXTERN HeapTuple RI_FKey_cascade_del(FmgrInfo *proinfo);
PG_EXTERN HeapTuple RI_FKey_cascade_upd(FmgrInfo *proinfo);
PG_EXTERN HeapTuple RI_FKey_restrict_del(FmgrInfo *proinfo);
PG_EXTERN HeapTuple RI_FKey_restrict_upd(FmgrInfo *proinfo);
PG_EXTERN HeapTuple RI_FKey_setnull_del(FmgrInfo *proinfo);
PG_EXTERN HeapTuple RI_FKey_setnull_upd(FmgrInfo *proinfo);
PG_EXTERN HeapTuple RI_FKey_setdefault_del(FmgrInfo *proinfo);
PG_EXTERN HeapTuple RI_FKey_setdefault_upd(FmgrInfo *proinfo);

 #ifdef __cplusplus
 }
 #endif 

#endif	 /* BUILTINS_H */
