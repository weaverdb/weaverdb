/*-------------------------------------------------------------------------
 *
 * pg_proc.h
 *	  definition of the system "procedure" relation (pg_proc)
 *	  along with the relation's initial contents.
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 * NOTES
 *	  The script catalog/genbki.sh reads this file and generates .bki
 *	  information from the DATA() statements.  utils/Gen_fmgrtab.sh
 *	  generates fmgr.h and fmgrtab.c the same way.
 *
 *	  XXX do NOT break up DATA() statements into multiple lines!
 *		  the scripts are not as smart as you might think...
 *	  XXX (eg. #if 0 #endif won't do what you think)
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_PROC_H
#define PG_PROC_H


/* ----------------
 *		postgres.h contains the system type definintions and the
 *		CATALOG(), BOOTSTRAP and DATA() sugar words so this file
 *		can be read by both genbki.sh and the C compiler.
 * ----------------
 */

/* ----------------
 *		pg_proc definition.  cpp turns this into
 *		typedef struct FormData_pg_proc
 * ----------------
 */
CATALOG(pg_proc) BOOTSTRAP
{
	NameData	proname;
	int4		proowner;
	Oid			prolang;
	bool		proisinh;
	bool		proistrusted;
	bool		proiscachable;
	int2		pronargs;
	bool		proretset;
	Oid			prorettype;
	oidvector	proargtypes;
	int4		probyte_pct;
	int4		properbyte_cpu;
	int4		propercall_cpu;
	int4		prooutin_ratio;
	text		prosrc;			/* VARIABLE LENGTH FIELD */
	bytea		probin;			/* VARIABLE LENGTH FIELD */
} FormData_pg_proc;

/* ----------------
 *		Form_pg_proc corresponds to a pointer to a tuple with
 *		the format of pg_proc relation.
 * ----------------
 */
typedef FormData_pg_proc *Form_pg_proc;

/* ----------------
 *		compiler constants for pg_proc
 * ----------------
 */
#define Natts_pg_proc					16
#define Anum_pg_proc_proname			1
#define Anum_pg_proc_proowner			2
#define Anum_pg_proc_prolang			3
#define Anum_pg_proc_proisinh			4
#define Anum_pg_proc_proistrusted		5
#define Anum_pg_proc_proiscachable		6
#define Anum_pg_proc_pronargs			7
#define Anum_pg_proc_proretset			8
#define Anum_pg_proc_prorettype			9
#define Anum_pg_proc_proargtypes		10
#define Anum_pg_proc_probyte_pct		11
#define Anum_pg_proc_properbyte_cpu		12
#define Anum_pg_proc_propercall_cpu		13
#define Anum_pg_proc_prooutin_ratio		14
#define Anum_pg_proc_prosrc				15
#define Anum_pg_proc_probin				16

/* ----------------
 *		initial contents of pg_proc
 * ----------------
 */

/* keep the following ordered by OID so that later changes can be made easier */

/* OIDS 1 - 99 */
DATA(insert OID = 12  (  update_pg_pwd	   PGUID 11 f t f 0 f 0  ""  100 0 0 100  update_pg_pwd - ));
DESCR("update pg_pwd file");

DATA(insert OID = 1242 (  boolin		   PGUID 11 f t t 1 f 16 "0" 100 0 0  100  boolin - ));
DESCR("(internal)");
DATA(insert OID = 1243 (  boolout		   PGUID 11 f t t 1 f 23 "0" 100 0 0 100  boolout - ));
DESCR("(internal)");
DATA(insert OID = 1244 (  byteain		   PGUID 11 f t t 1 f 17 "0" 100 0 0 100  byteain - ));
DESCR("(internal)");
DATA(insert OID =  31 (  byteaout		   PGUID 11 f t t 1 f 23 "0" 100 0 0 100  byteaout - ));
DESCR("(internal)");
DATA(insert OID = 1245 (  charin		   PGUID 11 f t t 1 f 18 "0" 100 0 0 100  charin - ));
DESCR("(internal)");
DATA(insert OID =  33 (  charout		   PGUID 11 f t t 1 f 23 "0" 100 0 0 100  charout - ));
DESCR("(internal)");
DATA(insert OID =  34 (  namein			   PGUID 11 f t t 1 f 19 "0" 100 0 0 100  namein - ));
DESCR("(internal)");
DATA(insert OID =  35 (  nameout		   PGUID 11 f t t 1 f 23 "0" 100 0 0 100  nameout - ));
DESCR("(internal)");
DATA(insert OID =  38 (  int2in			   PGUID 11 f t t 1 f 21 "0" 100 0 0 100  int2in - ));
DESCR("(internal)");
DATA(insert OID =  39 (  int2out		   PGUID 11 f t t 1 f 23 "0" 100 0 0 100  int2out - ));
DESCR("(internal)");
DATA(insert OID =  40 (  int2vectorin	   PGUID 11 f t t 1 f 22 "0" 100 0 0 100  int2vectorin - ));
DESCR("(internal)");
DATA(insert OID =  41 (  int2vectorout	   PGUID 11 f t t 1 f 23 "0" 100 0 0 100  int2vectorout - ));
DESCR("(internal)");
DATA(insert OID =  42 (  int4in			   PGUID 11 f t t 1 f 23 "0" 100 0 0 100  int4in - ));
DESCR("(internal)");
DATA(insert OID =  43 (  int4out		   PGUID 11 f t t 1 f 19 "0" 100 0 0 100  int4out - ));
DESCR("(internal)");
DATA(insert OID =  44 (  regprocin		   PGUID 11 f t f 1 f 24 "0" 100 0 0 100  regprocin - ));
DESCR("(internal)");
DATA(insert OID =  45 (  regprocout		   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  regprocout - ));
DESCR("(internal)");
DATA(insert OID =  46 (  textin			   PGUID 11 f t t 1 f 25 "0" 100 0 0 100  textin - ));
DESCR("(internal)");
DATA(insert OID =  47 (  textout		   PGUID 11 f t t 1 f 23 "0" 100 0 0 100  textout - ));
DESCR("(internal)");
DATA(insert OID =  48 (  tidin			   PGUID 11 f t t 1 f 27 "0" 100 0 0 100  tidin - ));
DESCR("(internal)");
DATA(insert OID =  49 (  tidout			   PGUID 11 f t t 1 f 23 "0" 100 0 0 100  tidout - ));
DESCR("(internal)");
DATA(insert OID =  50 (  xidin			   PGUID 11 f t t 1 f 28 "0" 100 0 0 100  xidin - ));
DESCR("(internal)");
DATA(insert OID =  51 (  xidout			   PGUID 11 f t t 1 f 23 "0" 100 0 0 100  xidout - ));
DESCR("(internal)");
DATA(insert OID =  52 (  cidin			   PGUID 11 f t t 1 f 29 "0" 100 0 0 100  cidin - ));
DESCR("(internal)");
DATA(insert OID =  53 (  cidout			   PGUID 11 f t t 1 f 23 "0" 100 0 0 100  cidout - ));
DESCR("(internal)");
DATA(insert OID =  54 (  oidvectorin	   PGUID 11 f t t 1 f 30 "0" 100 0 0 100  oidvectorin - ));
DESCR("(internal)");
DATA(insert OID =  55 (  oidvectorout	   PGUID 11 f t t 1 f 23 "0" 100 0 0 100  oidvectorout - ));
DESCR("(internal)");
DATA(insert OID =  56 (  boollt			   PGUID 11 f t t 2 f 16 "16 16" 100 0 0 100  boollt - ));
DESCR("less-than");
DATA(insert OID =  57 (  boolgt			   PGUID 11 f t t 2 f 16 "16 16" 100 0 0 100  boolgt - ));
DESCR("greater-than");
DATA(insert OID =  60 (  booleq			   PGUID 11 f t t 2 f 16 "16 16" 100 0 0 100  booleq - ));
DESCR("equal");
DATA(insert OID =  61 (  chareq			   PGUID 11 f t t 2 f 16 "18 18" 100 0 0 100  chareq - ));
DESCR("equal");
DATA(insert OID =  62 (  nameeq			   PGUID 11 f t t 2 f 16 "19 19" 100 0 0 100  nameeq - ));
DESCR("equal");
DATA(insert OID =  63 (  int2eq			   PGUID 11 f t t 2 f 16 "21 21" 100 0 0 100  int2eq - ));
DESCR("equal");
DATA(insert OID =  64 (  int2lt			   PGUID 11 f t t 2 f 16 "21 21" 100 0 0 100  int2lt - ));
DESCR("less-than");
DATA(insert OID =  65 (  int4eq			   PGUID 11 f t t 2 f 16 "23 23" 100 0 0 100  int4eq - ));
DESCR("equal");
DATA(insert OID =  66 (  int4lt			   PGUID 11 f t t 2 f 16 "23 23" 100 0 0 100  int4lt - ));
DESCR("less-than");
DATA(insert OID =  67 (  texteq			   PGUID 11 f t t 2 f 16 "25 25" 100 0 0 0	texteq - ));
DESCR("equal");
DATA(insert OID =  68 (  xideq			   PGUID 11 f t t 2 f 16 "28 28" 100 0 0 100  xideq - ));
DESCR("equal");
DATA(insert OID =  69 (  cideq			   PGUID 11 f t t 2 f 16 "29 29" 100 0 0 100  cideq - ));
DESCR("equal");
DATA(insert OID =  70 (  charne			   PGUID 11 f t t 2 f 16 "18 18" 100 0 0 100  charne - ));
DESCR("not equal");
DATA(insert OID = 1246 (  charlt		   PGUID 11 f t t 2 f 16 "18 18" 100 0 0 100  charlt - ));
DESCR("less-than");
DATA(insert OID =  72 (  charle			   PGUID 11 f t t 2 f 16 "18 18" 100 0 0 100  charle - ));
DESCR("less-than-or-equal");
DATA(insert OID =  73 (  chargt			   PGUID 11 f t t 2 f 16 "18 18" 100 0 0 100  chargt - ));
DESCR("greater-than");
DATA(insert OID =  74 (  charge			   PGUID 11 f t t 2 f 16 "18 18" 100 0 0 100  charge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 1248 (  charpl		   PGUID 11 f t t 2 f 18 "18 18" 100 0 0 100  charpl - ));
DESCR("addition");
DATA(insert OID = 1250 (  charmi		   PGUID 11 f t t 2 f 18 "18 18" 100 0 0 100  charmi - ));
DESCR("subtract");
DATA(insert OID =  77 (  charmul		   PGUID 11 f t t 2 f 18 "18 18" 100 0 0 100  charmul - ));
DESCR("multiply");
DATA(insert OID =  78 (  chardiv		   PGUID 11 f t t 2 f 18 "18 18" 100 0 0 100  chardiv - ));
DESCR("divide");

DATA(insert OID =  79 (  nameregexeq	   PGUID 11 f t t 2 f 16 "19 25" 100 0 0 100  nameregexeq - ));
DESCR("matches regex., case-sensitive");
DATA(insert OID = 1252 (  nameregexne	   PGUID 11 f t t 2 f 16 "19 25" 100 0 0 100  nameregexne - ));
DESCR("does not match regex., case-sensitive");
DATA(insert OID = 1254 (  textregexeq	   PGUID 11 f t t 2 f 16 "25 25" 100 0 1 0	textregexeq - ));
DESCR("matches regex., case-sensitive");
DATA(insert OID = 1256 (  textregexne	   PGUID 11 f t t 2 f 16 "25 25" 100 0 1 0	textregexne - ));
DESCR("does not match regex., case-sensitive");
DATA(insert OID = 1257 (  textlen		   PGUID 11 f t t 1 f 23 "25" 100 0 1 0  textlen - ));
DESCR("length");
DATA(insert OID = 1258 (  textcat		   PGUID 11 f t t 2 f 25 "25 25" 100 0 1 0	textcat - ));
DESCR("concatenate");

DATA(insert OID =  84 (  boolne			   PGUID 11 f t t 2 f 16 "16 16" 100 0 0 100  boolne - ));
DESCR("not equal");
DATA(insert OID =  89 (  version		   PGUID 11 f t f 0 f 25 "" 100 0 0 100 version - ));
DESCR("PostgreSQL version string");
/*
DATA(insert OID = 1265 (  rtcostestimate   PGUID 11 f t f 7 f 0 "0 0 0 0 0 0 0" 100 0 0 100  rtcostestimate - ));
DESCR("r-tree cost estimator");
*/
DATA(insert OID = 1268 (  btcostestimate   PGUID 11 f t f 7 f 0 "0 0 0 0 0 0 0" 100 0 0 100  btcostestimate - ));
DESCR("btree cost estimator");

DATA(insert OID = 764 (  oidin			PGUID 11 f t t 1 f 26 "0" 100 0 0 100  oidin - ));
DESCR("(internal)");
DATA(insert OID = 765 (  oidout			PGUID 11 f t t 1 f 19 "0" 100 0 0 100  oidout - ));
DESCR("(internal)");

DATA(insert OID = 953 (  longin			PGUID 11 f t t 1 f 952 "0" 100 0 0 100  longin - ));
DESCR("(internal)");
DATA(insert OID = 954 (  longout			PGUID 11 f t t 1 f 19 "0" 100 0 0 100  longout - ));
DESCR("(internal)");
DATA(insert OID = 955 (  longeqoid			PGUID 11 f t t 1 f 16 "952 26" 100 0 0 100  longeqoid - ));
DESCR("(internal)");
DATA(insert OID = 956 (  oideqlong			PGUID 11 f t t 1 f 16 "26 952" 100 0 0 100  oideqlong - ));
DESCR("(internal)");

/* OIDS 100 - 199 */

DATA(insert OID = 100 (  int8fac		   PGUID 11 f t t 1 f 20 "20" 100 0 0 100  int8fac - ));
DESCR("factorial");
DATA(insert OID = 101 (  eqsel			   PGUID 11 f t f 5 f 701 "26 26 21 0 23" 100 0 0 100  eqsel - ));
DESCR("restriction selectivity of = and related operators");
DATA(insert OID = 102 (  neqsel			   PGUID 11 f t f 5 f 701 "26 26 21 0 23" 100 0 0 100  neqsel - ));
DESCR("restriction selectivity of <> and related operators");
DATA(insert OID = 103 (  scalarltsel	   PGUID 11 f t f 5 f 701 "26 26 21 0 23" 100 0 0 100  scalarltsel - ));
DESCR("restriction selectivity of < and related operators on scalar datatypes");
DATA(insert OID = 104 (  scalargtsel	   PGUID 11 f t f 5 f 701 "26 26 21 0 23" 100 0 0 100  scalargtsel - ));
DESCR("restriction selectivity of > and related operators on scalar datatypes");
DATA(insert OID = 105 (  eqjoinsel		   PGUID 11 f t f 5 f 701 "26 26 21 26 21" 100 0 0 100	eqjoinsel - ));
DESCR("join selectivity of = and related operators");
DATA(insert OID = 106 (  neqjoinsel		   PGUID 11 f t f 5 f 701 "26 26 21 26 21" 100 0 0 100	neqjoinsel - ));
DESCR("join selectivity of <> and related operators");
DATA(insert OID = 107 (  scalarltjoinsel   PGUID 11 f t f 5 f 701 "26 26 21 26 21" 100 0 0 100	scalarltjoinsel - ));
DESCR("join selectivity of < and related operators on scalar datatypes");
DATA(insert OID = 108 (  scalargtjoinsel   PGUID 11 f t f 5 f 701 "26 26 21 26 21" 100 0 0 100	scalargtjoinsel - ));
DESCR("join selectivity of > and related operators on scalar datatypes");

DATA(insert OID = 112 (  text			   PGUID 11 f t t 1 f  25 "23" 100 0 0 100	int4_text - ));
DESCR("convert int4 to text");
DATA(insert OID = 113 (  text			   PGUID 11 f t t 1 f  25 "21" 100 0 0 100	int2_text - ));
DESCR("convert int2 to text");
DATA(insert OID = 114 (  text			   PGUID 11 f t t 1 f  25 "26" 100 0 0 100	oid_text - ));
DESCR("convert oid to text");
#ifdef USEGEO
DATA(insert OID = 115 (  rect_above		   PGUID 11 f t t 2 f  16 "603 603" 100 1 0 100  rect_above - ));
DESCR("is above");
DATA(insert OID = 116 (  rect_below		   PGUID 11 f t t 2 f  16 "603 603" 100 1 0 100  rect_below - ));
DESCR("is below");

DATA(insert OID = 117 (  point_in		   PGUID 11 f t t 1 f 600 "0" 100 0 0 100  point_in - ));
DESCR("(internal)");
DATA(insert OID = 118 (  point_out		   PGUID 11 f t t 1 f 23  "0" 100 0 0 100  point_out - ));
DESCR("(internal)");
DATA(insert OID = 119 (  lseg_in		   PGUID 11 f t t 1 f 601 "0" 100 0 0 100  lseg_in - ));
DESCR("(internal)");
DATA(insert OID = 120 (  lseg_out		   PGUID 11 f t t 1 f 23  "0" 100 0 0 100  lseg_out - ));
DESCR("(internal)");
DATA(insert OID = 121 (  path_in		   PGUID 11 f t t 1 f 602 "0" 100 0 0 100  path_in - ));
DESCR("(internal)");
DATA(insert OID = 122 (  path_out		   PGUID 11 f t t 1 f 23  "0" 100 0 0 100  path_out - ));
DESCR("(internal)");
DATA(insert OID = 123 (  rect_in			   PGUID 11 f t t 1 f 603 "0" 100 0 0 100  rect_in - ));
DESCR("(internal)");
DATA(insert OID = 124 (  rect_out		   PGUID 11 f t t 1 f 23  "0" 100 0 0 100  rect_out - ));
DESCR("(internal)");
DATA(insert OID = 125 (  rect_overlap	   PGUID 11 f t t 2 f 16 "603 603" 100 1 0 100	rect_overlap - ));
DESCR("overlaps");
DATA(insert OID = 126 (  rect_ge			   PGUID 11 f t t 2 f 16 "603 603" 100 1 0 100	rect_ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 127 (  rect_gt			   PGUID 11 f t t 2 f 16 "603 603" 100 1 0 100	rect_gt - ));
DESCR("greater-than");
DATA(insert OID = 128 (  rect_eq			   PGUID 11 f t t 2 f 16 "603 603" 100 1 0 100	rect_eq - ));
DESCR("equal");
DATA(insert OID = 129 (  rect_lt			   PGUID 11 f t t 2 f 16 "603 603" 100 1 0 100	rect_lt - ));
DESCR("less-than");
DATA(insert OID = 130 (  rect_le			   PGUID 11 f t t 2 f 16 "603 603" 100 1 0 100	rect_le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 131 (  point_above	   PGUID 11 f t t 2 f 16 "600 600" 100 0 0 100	point_above - ));
DESCR("is above");
DATA(insert OID = 132 (  point_left		   PGUID 11 f t t 2 f 16 "600 600" 100 0 0 100	point_left - ));
DESCR("is left of");
DATA(insert OID = 133 (  point_right	   PGUID 11 f t t 2 f 16 "600 600" 100 0 0 100	point_right - ));
DESCR("is left of");
DATA(insert OID = 134 (  point_below	   PGUID 11 f t t 2 f 16 "600 600" 100 0 0 100	point_below - ));
DESCR("is below");
DATA(insert OID = 135 (  point_eq		   PGUID 11 f t t 2 f 16 "600 600" 100 0 0 100	point_eq - ));
DESCR("same as");
DATA(insert OID = 136 (  on_pb			   PGUID 11 f t t 2 f 16 "600 603" 100 0 0 100	on_pb - ));
DESCR("point is inside");
DATA(insert OID = 137 (  on_ppath		   PGUID 11 f t t 2 f 16 "600 602" 100 0 1 0  on_ppath - ));
DESCR("contained in");
DATA(insert OID = 138 (  rect_center		   PGUID 11 f t t 1 f 600 "603" 100 1 0 100  rect_center - ));
DESCR("center of");
DATA(insert OID = 139 (  areasel		   PGUID 11 f t f 5 f 701 "26 26 21 0 23" 100 0 0 100  areasel - ));
DESCR("restriction selectivity for area-comparison operators");
DATA(insert OID = 140 (  areajoinsel	   PGUID 11 f t f 5 f 701 "26 26 21 26 21" 100 0 0 100	areajoinsel - ));
DESCR("join selectivity for area-comparison operators");
#endif /* USEGEO  */
DATA(insert OID = 141 (  int4mul		   PGUID 11 f t t 2 f 23 "23 23" 100 0 0 100  int4mul - ));
DESCR("multiply");
DATA(insert OID = 142 (  int4fac		   PGUID 11 f t t 1 f 23 "23" 100 0 0 100  int4fac - ));
DESCR("factorial");
#ifdef USEGEO
DATA(insert OID = 143 (  pointdist		   PGUID 11 f t t 2 f 23 "600 600" 100 0 0 100	pointdist - ));
DESCR("");
#endif /* USEGEO */
DATA(insert OID = 144 (  int4ne			   PGUID 11 f t t 2 f 16 "23 23" 100 0 0 100  int4ne - ));
DESCR("not equal");
DATA(insert OID = 145 (  int2ne			   PGUID 11 f t t 2 f 16 "21 21" 100 0 0 100  int2ne - ));
DESCR("not equal");
DATA(insert OID = 146 (  int2gt			   PGUID 11 f t t 2 f 16 "21 21" 100 0 0 100  int2gt - ));
DESCR("greater-than");
DATA(insert OID = 147 (  int4gt			   PGUID 11 f t t 2 f 16 "23 23" 100 0 0 100  int4gt - ));
DESCR("greater-than");
DATA(insert OID = 148 (  int2le			   PGUID 11 f t t 2 f 16 "21 21" 100 0 0 100  int2le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 149 (  int4le			   PGUID 11 f t t 2 f 16 "23 23" 100 0 0 100  int4le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 150 (  int4ge			   PGUID 11 f t t 2 f 16 "23 23" 100 0 0 100  int4ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 151 (  int2ge			   PGUID 11 f t t 2 f 16 "21 21" 100 0 0 100  int2ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 152 (  int2mul		   PGUID 11 f t t 2 f 21 "21 21" 100 0 0 100  int2mul - ));
DESCR("multiply");
DATA(insert OID = 153 (  int2div		   PGUID 11 f t t 2 f 21 "21 21" 100 0 0 100  int2div - ));
DESCR("divide");
DATA(insert OID = 154 (  int4div		   PGUID 11 f t t 2 f 23 "23 23" 100 0 0 100  int4div - ));
DESCR("divide");
DATA(insert OID = 155 (  int2mod		   PGUID 11 f t t 2 f 21 "21 21" 100 0 0 100  int2mod - ));
DESCR("modulus");
DATA(insert OID = 156 (  int4mod		   PGUID 11 f t t 2 f 23 "23 23" 100 0 0 100  int4mod - ));
DESCR("modulus");
DATA(insert OID = 157 (  textne			   PGUID 11 f t t 2 f 16 "25 25" 100 0 0 0	textne - ));
DESCR("not equal");
DATA(insert OID = 158 (  int24eq		   PGUID 11 f t t 2 f 16 "21 23" 100 0 0 100  int24eq - ));
DESCR("equal");
DATA(insert OID = 159 (  int42eq		   PGUID 11 f t t 2 f 16 "23 21" 100 0 0 100  int42eq - ));
DESCR("equal");
DATA(insert OID = 160 (  int24lt		   PGUID 11 f t t 2 f 16 "21 23" 100 0 0 100  int24lt - ));
DESCR("less-than");
DATA(insert OID = 161 (  int42lt		   PGUID 11 f t t 2 f 16 "23 21" 100 0 0 100  int42lt - ));
DESCR("less-than");
DATA(insert OID = 162 (  int24gt		   PGUID 11 f t t 2 f 16 "21 23" 100 0 0 100  int24gt - ));
DESCR("greater-than");
DATA(insert OID = 163 (  int42gt		   PGUID 11 f t t 2 f 16 "23 21" 100 0 0 100  int42gt - ));
DESCR("greater-than");
DATA(insert OID = 164 (  int24ne		   PGUID 11 f t t 2 f 16 "21 23" 100 0 0 100  int24ne - ));
DESCR("not equal");
DATA(insert OID = 165 (  int42ne		   PGUID 11 f t t 2 f 16 "23 21" 100 0 0 100  int42ne - ));
DESCR("not equal");
DATA(insert OID = 166 (  int24le		   PGUID 11 f t t 2 f 16 "21 23" 100 0 0 100  int24le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 167 (  int42le		   PGUID 11 f t t 2 f 16 "23 21" 100 0 0 100  int42le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 168 (  int24ge		   PGUID 11 f t t 2 f 16 "21 23" 100 0 0 100  int24ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 169 (  int42ge		   PGUID 11 f t t 2 f 16 "23 21" 100 0 0 100  int42ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 170 (  int24mul		   PGUID 11 f t t 2 f 23 "21 23" 100 0 0 100  int24mul - ));
DESCR("multiply");
DATA(insert OID = 171 (  int42mul		   PGUID 11 f t t 2 f 23 "23 21" 100 0 0 100  int42mul - ));
DESCR("multiply");
DATA(insert OID = 172 (  int24div		   PGUID 11 f t t 2 f 23 "21 23" 100 0 0 100  int24div - ));
DESCR("divide");
DATA(insert OID = 173 (  int42div		   PGUID 11 f t t 2 f 23 "23 21" 100 0 0 100  int42div - ));
DESCR("divide");
DATA(insert OID = 174 (  int24mod		   PGUID 11 f t t 2 f 23 "21 23" 100 0 0 100  int24mod - ));
DESCR("modulus");
DATA(insert OID = 175 (  int42mod		   PGUID 11 f t t 2 f 23 "23 21" 100 0 0 100  int42mod - ));
DESCR("modulus");
DATA(insert OID = 176 (  int2pl			   PGUID 11 f t t 2 f 21 "21 21" 100 0 0 100  int2pl - ));
DESCR("addition");
DATA(insert OID = 177 (  int4pl			   PGUID 11 f t t 2 f 23 "23 23" 100 0 0 100  int4pl - ));
DESCR("addition");
DATA(insert OID = 178 (  int24pl		   PGUID 11 f t t 2 f 23 "21 23" 100 0 0 100  int24pl - ));
DESCR("addition");
DATA(insert OID = 179 (  int42pl		   PGUID 11 f t t 2 f 23 "23 21" 100 0 0 100  int42pl - ));
DESCR("addition");
DATA(insert OID = 180 (  int2mi			   PGUID 11 f t t 2 f 21 "21 21" 100 0 0 100  int2mi - ));
DESCR("subtract");
DATA(insert OID = 181 (  int4mi			   PGUID 11 f t t 2 f 23 "23 23" 100 0 0 100  int4mi - ));
DESCR("subtract");
DATA(insert OID = 182 (  int24mi		   PGUID 11 f t t 2 f 23 "21 23" 100 0 0 100  int24mi - ));
DESCR("subtract");
DATA(insert OID = 183 (  int42mi		   PGUID 11 f t t 2 f 23 "23 21" 100 0 0 100  int42mi - ));
DESCR("subtract");
DATA(insert OID = 184 (  oideq			   PGUID 11 f t t 2 f 16 "26 26" 100 0 0 100  oideq - ));
DESCR("equal");
DATA(insert OID = 185 (  oidne			   PGUID 11 f t t 2 f 16 "26 26" 100 0 0 100  oidne - ));
DESCR("not equal");
#ifdef USEGEO
DATA(insert OID = 186 (  rect_same		   PGUID 11 f t t 2 f 16 "603 603" 100 0 0 100	rect_same - ));
DESCR("same as");
DATA(insert OID = 187 (  rect_contain	   PGUID 11 f t t 2 f 16 "603 603" 100 0 0 100	rect_contain - ));
DESCR("contains");
DATA(insert OID = 188 (  rect_left		   PGUID 11 f t t 2 f 16 "603 603" 100 0 0 100	rect_left - ));
DESCR("is left of");
DATA(insert OID = 189 (  rect_overleft	   PGUID 11 f t t 2 f 16 "603 603" 100 0 0 100	rect_overleft - ));
DESCR("overlaps, but does not extend to right of");
DATA(insert OID = 190 (  rect_overright	   PGUID 11 f t t 2 f 16 "603 603" 100 0 0 100	rect_overright - ));
DESCR("overlaps, but does not extend to left of");
DATA(insert OID = 191 (  rect_right		   PGUID 11 f t t 2 f 16 "603 603" 100 0 0 100	rect_right - ));
DESCR("is left of");
DATA(insert OID = 192 (  rect_contained	   PGUID 11 f t t 2 f 16 "603 603" 100 0 0 100	rect_contained - ));
DESCR("contained in");
DATA(insert OID = 193 (  rt_rect_union	   PGUID 11 f t t 2 f 603 "603 603" 100 0 0 100  rt_rect_union - ));
DESCR("r-tree");
DATA(insert OID = 194 (  rt_rect_inter	   PGUID 11 f t t 2 f 603 "603 603" 100 0 0 100  rt_rect_inter - ));
DESCR("r-tree");
DATA(insert OID = 195 (  rt_rect_size	   PGUID 11 f t t 2 f 700 "603 700" 100 0 0 100  rt_rect_size - ));
DESCR("r-tree");
DATA(insert OID = 196 (  rt_bigbox_size    PGUID 11 f t t 2 f 700 "603 700" 100 0 0 100  rt_bigbox_size - ));
DESCR("r-tree");
DATA(insert OID = 197 (  rt_poly_union	   PGUID 11 f t t 2 f 604 "604 604" 100 0 0 100  rt_poly_union - ));
DESCR("r-tree");
DATA(insert OID = 198 (  rt_poly_inter	   PGUID 11 f t t 2 f 604 "604 604" 100 0 0 100  rt_poly_inter - ));
DESCR("r-tree");
DATA(insert OID = 199 (  rt_poly_size	   PGUID 11 f t t 2 f 23 "604 23" 100 0 0 100  rt_poly_size - ));
DESCR("r-tree");
#endif /* USEGEO */
/* OIDS 200 - 299 */

DATA(insert OID = 200 (  float4in		   PGUID 11 f t t 1 f 700 "0" 100 0 0 100  float4in - ));
DESCR("(internal)");
DATA(insert OID = 201 (  float4out		   PGUID 11 f t t 1 f 23  "0" 100 0 0 100  float4out - ));
DESCR("(internal)");
DATA(insert OID = 202 (  float4mul		   PGUID 11 f t t 2 f 700 "700 700" 100 0 0 100  float4mul - ));
DESCR("multiply");
DATA(insert OID = 203 (  float4div		   PGUID 11 f t t 2 f 700 "700 700" 100 0 0 100  float4div - ));
DESCR("divide");
DATA(insert OID = 204 (  float4pl		   PGUID 11 f t t 2 f 700 "700 700" 100 0 0 100  float4pl - ));
DESCR("addition");
DATA(insert OID = 205 (  float4mi		   PGUID 11 f t t 2 f 700 "700 700" 100 0 0 100  float4mi - ));
DESCR("subtract");
DATA(insert OID = 206 (  float4um		   PGUID 11 f t t 1 f 700 "700" 100 0 0 100  float4um - ));
DESCR("negate");
DATA(insert OID = 207 (  float4abs		   PGUID 11 f t t 1 f 700 "700" 100 0 0 100  float4abs - ));
DESCR("absolute value");
DATA(insert OID = 208 (  float4inc		   PGUID 11 f t t 1 f 700 "700" 100 0 0 100  float4inc - ));
DESCR("increment");
DATA(insert OID = 209 (  float4larger	   PGUID 11 f t t 2 f 700 "700 700" 100 0 0 100  float4larger - ));
DESCR("larger of two");
DATA(insert OID = 211 (  float4smaller	   PGUID 11 f t t 2 f 700 "700 700" 100 0 0 100  float4smaller - ));
DESCR("smaller of two");

DATA(insert OID = 212 (  int4um			   PGUID 11 f t t 1 f 23 "23" 100 0 0 100  int4um - ));
DESCR("negate");
DATA(insert OID = 213 (  int2um			   PGUID 11 f t t 1 f 21 "21" 100 0 0 100  int2um - ));
DESCR("negate");

DATA(insert OID = 214 (  float8in		   PGUID 11 f t t 1 f 701 "0" 100 0 0 100  float8in - ));
DESCR("(internal)");
DATA(insert OID = 215 (  float8out		   PGUID 11 f t t 1 f 23  "0" 100 0 0 100  float8out - ));
DESCR("(internal)");
DATA(insert OID = 216 (  float8mul		   PGUID 11 f t t 2 f 701 "701 701" 100 0 0 100  float8mul - ));
DESCR("multiply");
DATA(insert OID = 217 (  float8div		   PGUID 11 f t t 2 f 701 "701 701" 100 0 0 100  float8div - ));
DESCR("divide");
DATA(insert OID = 218 (  float8pl		   PGUID 11 f t t 2 f 701 "701 701" 100 0 0 100  float8pl - ));
DESCR("addition");
DATA(insert OID = 219 (  float8mi		   PGUID 11 f t t 2 f 701 "701 701" 100 0 0 100  float8mi - ));
DESCR("subtract");
DATA(insert OID = 220 (  float8um		   PGUID 11 f t t 1 f 701 "701" 100 0 0 100  float8um - ));
DESCR("negate");
DATA(insert OID = 221 (  float8abs		   PGUID 11 f t t 1 f 701 "701" 100 0 0 100  float8abs - ));
DESCR("absolute value");
DATA(insert OID = 222 (  float8inc		   PGUID 11 f t t 1 f 701 "701" 100 0 0 100  float8inc - ));
DESCR("increment");
DATA(insert OID = 223 (  float8larger	   PGUID 11 f t t 2 f 701 "701 701" 100 0 0 100  float8larger - ));
DESCR("larger of two");
DATA(insert OID = 224 (  float8smaller	   PGUID 11 f t t 2 f 701 "701 701" 100 0 0 100  float8smaller - ));
DESCR("smaller of two");
#ifdef USEGEO
DATA(insert OID = 225 (  lseg_center	   PGUID 11 f t t 1 f 600 "601" 100 0 0 100  lseg_center - ));
DESCR("center of");
DATA(insert OID = 226 (  path_center	   PGUID 11 f t t 1 f 600 "602" 100 0 0 100  path_center - ));
DESCR("center of");
DATA(insert OID = 227 (  poly_center	   PGUID 11 f t t 1 f 600 "604" 100 0 0 100  poly_center - ));
DESCR("center of");
#endif
DATA(insert OID = 228 (  dround			   PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dround - ));
DESCR("round to integer");
DATA(insert OID = 229 (  dtrunc			   PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dtrunc - ));
DESCR("truncate to integer");
DATA(insert OID = 230 (  dsqrt			   PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dsqrt - ));
DESCR("square root");
DATA(insert OID = 231 (  dcbrt			   PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dcbrt - ));
DESCR("cube root");
DATA(insert OID = 232 (  dpow			   PGUID 11 f t t 2 f 701 "701 701" 100 0 0 100  dpow - ));
DESCR("exponentiation (x^y)");
DATA(insert OID = 233 (  dexp			   PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dexp - ));
DESCR("natural exponential (e^x)");
DATA(insert OID = 234 (  dlog1			   PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dlog1 - ));
DESCR("natural logarithm");
DATA(insert OID = 235 (  float8			   PGUID 11 f t t 1 f 701  "21" 100 0 0 100  i2tod - ));
DESCR("convert int2 to float8");
DATA(insert OID = 236 (  float4			   PGUID 11 f t t 1 f 700  "21" 100 0 0 100  i2tof - ));
DESCR("convert int2 to float4");
DATA(insert OID = 237 (  int2			   PGUID 11 f t t 1 f  21 "701" 100 0 0 100  dtoi2 - ));
DESCR("convert float8 to int2");
DATA(insert OID = 238 (  int2			   PGUID 11 f t t 1 f  21 "700" 100 0 0 100  ftoi2 - ));
DESCR("convert float4 to int2");
#ifdef USEGEO
DATA(insert OID = 239 (  line_distance	   PGUID 11 f t t 2 f 701 "628 628" 100 0 0 100  line_distance - ));
DESCR("distance between");
#endif

DATA(insert OID = 240 (  nabstimein		   PGUID 11 f t f 1 f 702 "0" 100 0 0 100  nabstimein - ));
DESCR("(internal)");
DATA(insert OID = 241 (  nabstimeout	   PGUID 11 f t f 1 f 23  "0" 100 0 0 100  nabstimeout - ));
DESCR("(internal)");
DATA(insert OID = 242 (  reltimein		   PGUID 11 f t f 1 f 703 "0" 100 0 0 100  reltimein - ));
DESCR("(internal)");
DATA(insert OID = 243 (  reltimeout		   PGUID 11 f t f 1 f 23  "0" 100 0 0 100  reltimeout - ));
DESCR("(internal)");
DATA(insert OID = 244 (  timepl			   PGUID 11 f t f 2 f 702 "702 703" 100 0 0 100  timepl - ));
DESCR("addition");
DATA(insert OID = 245 (  timemi			   PGUID 11 f t f 2 f 702 "702 703" 100 0 0 100  timemi - ));
DESCR("subtract");
DATA(insert OID = 246 (  tintervalin	   PGUID 11 f t f 1 f 704 "0" 100 0 0 100  tintervalin - ));
DESCR("(internal)");
DATA(insert OID = 247 (  tintervalout	   PGUID 11 f t f 1 f 23  "0" 100 0 0 100  tintervalout - ));
DESCR("(internal)");
DATA(insert OID = 248 (  intinterval	   PGUID 11 f t f 2 f 16 "702 704" 100 0 0 100	intinterval - ));
DESCR("abstime in tinterval");
DATA(insert OID = 249 (  tintervalrel	   PGUID 11 f t f 1 f 703 "704" 100 0 0 100  tintervalrel - ));
DESCR("");
DATA(insert OID = 250 (  timenow		   PGUID 11 f t f 0 f 702 "0" 100 0 0 100  timenow - ));
DESCR("Current date and time (abstime)");
DATA(insert OID = 251 (  abstimeeq		   PGUID 11 f t f 2 f 16 "702 702" 100 0 0 100	abstimeeq - ));
DESCR("equal");
DATA(insert OID = 252 (  abstimene		   PGUID 11 f t f 2 f 16 "702 702" 100 0 0 100	abstimene - ));
DESCR("not equal");
DATA(insert OID = 253 (  abstimelt		   PGUID 11 f t f 2 f 16 "702 702" 100 0 0 100	abstimelt - ));
DESCR("less-than");
DATA(insert OID = 254 (  abstimegt		   PGUID 11 f t f 2 f 16 "702 702" 100 0 0 100	abstimegt - ));
DESCR("greater-than");
DATA(insert OID = 255 (  abstimele		   PGUID 11 f t f 2 f 16 "702 702" 100 0 0 100	abstimele - ));
DESCR("less-than-or-equal");
DATA(insert OID = 256 (  abstimege		   PGUID 11 f t f 2 f 16 "702 702" 100 0 0 100	abstimege - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 257 (  reltimeeq		   PGUID 11 f t t 2 f 16 "703 703" 100 0 0 100	reltimeeq - ));
DESCR("equal");
DATA(insert OID = 258 (  reltimene		   PGUID 11 f t t 2 f 16 "703 703" 100 0 0 100	reltimene - ));
DESCR("not equal");
DATA(insert OID = 259 (  reltimelt		   PGUID 11 f t t 2 f 16 "703 703" 100 0 0 100	reltimelt - ));
DESCR("less-than");
DATA(insert OID = 260 (  reltimegt		   PGUID 11 f t t 2 f 16 "703 703" 100 0 0 100	reltimegt - ));
DESCR("greater-than");
DATA(insert OID = 261 (  reltimele		   PGUID 11 f t t 2 f 16 "703 703" 100 0 0 100	reltimele - ));
DESCR("less-than-or-equal");
DATA(insert OID = 262 (  reltimege		   PGUID 11 f t t 2 f 16 "703 703" 100 0 0 100	reltimege - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 263 (  tintervalsame	   PGUID 11 f t f 2 f 16 "704 704" 100 0 0 100	tintervalsame - ));
DESCR("same as");
DATA(insert OID = 264 (  tintervalct	   PGUID 11 f t f 2 f 16 "704 704" 100 0 0 100	tintervalct - ));
DESCR("less-than");
DATA(insert OID = 265 (  tintervalov	   PGUID 11 f t f 2 f 16 "704 704" 100 0 0 100	tintervalov - ));
DESCR("overlaps");
DATA(insert OID = 266 (  tintervalleneq    PGUID 11 f t f 2 f 16 "704 703" 100 0 0 100	tintervalleneq - ));
DESCR("length equal");
DATA(insert OID = 267 (  tintervallenne    PGUID 11 f t f 2 f 16 "704 703" 100 0 0 100	tintervallenne - ));
DESCR("length not equal to");
DATA(insert OID = 268 (  tintervallenlt    PGUID 11 f t f 2 f 16 "704 703" 100 0 0 100	tintervallenlt - ));
DESCR("length less-than");
DATA(insert OID = 269 (  tintervallengt    PGUID 11 f t f 2 f 16 "704 703" 100 0 0 100	tintervallengt - ));
DESCR("length greater-than");
DATA(insert OID = 270 (  tintervallenle    PGUID 11 f t f 2 f 16 "704 703" 100 0 0 100	tintervallenle - ));
DESCR("length less-than-or-equal");
DATA(insert OID = 271 (  tintervallenge    PGUID 11 f t f 2 f 16 "704 703" 100 0 0 100	tintervallenge - ));
DESCR("length greater-than-or-equal");
DATA(insert OID = 272 (  tintervalstart    PGUID 11 f t f 1 f 702 "704" 100 0 0 100  tintervalstart - ));
DESCR("start of interval");
DATA(insert OID = 273 (  tintervalend	   PGUID 11 f t f 1 f 702 "704" 100 0 0 100  tintervalend - ));
DESCR("");
DATA(insert OID = 274 (  timeofday		   PGUID 11 f t f 0 f 25 "0" 100 0 0 100  timeofday - ));
DESCR("Current date and time with microseconds");
DATA(insert OID = 275 (  isfinite		   PGUID 11 f t f 1 f 16 "702" 100 0 0 100	abstime_finite - ));
DESCR("");

DATA(insert OID = 276 (  int2fac		   PGUID 11 f t t 1 f 23 "21" 100 0 0 100  int2fac - ));
DESCR("");
#ifdef USEGEO
DATA(insert OID = 277 (  inter_sl		   PGUID 11 f t t 2 f 16 "601 628" 100 0 0 100	inter_sl - ));
DESCR("");
DATA(insert OID = 278 (  inter_lb		   PGUID 11 f t t 2 f 16 "628 603" 100 0 0 100	inter_lb - ));
DESCR("");
#endif

DATA(insert OID = 279 (  float48mul		   PGUID 11 f t t 2 f 701 "700 701" 100 0 0 100  float48mul - ));
DESCR("multiply");
DATA(insert OID = 280 (  float48div		   PGUID 11 f t t 2 f 701 "700 701" 100 0 0 100  float48div - ));
DESCR("divide");
DATA(insert OID = 281 (  float48pl		   PGUID 11 f t t 2 f 701 "700 701" 100 0 0 100  float48pl - ));
DESCR("addition");
DATA(insert OID = 282 (  float48mi		   PGUID 11 f t t 2 f 701 "700 701" 100 0 0 100  float48mi - ));
DESCR("subtract");
DATA(insert OID = 283 (  float84mul		   PGUID 11 f t t 2 f 701 "701 700" 100 0 0 100  float84mul - ));
DESCR("multiply");
DATA(insert OID = 284 (  float84div		   PGUID 11 f t t 2 f 701 "701 700" 100 0 0 100  float84div - ));
DESCR("divide");
DATA(insert OID = 285 (  float84pl		   PGUID 11 f t t 2 f 701 "701 700" 100 0 0 100  float84pl - ));
DESCR("addition");
DATA(insert OID = 286 (  float84mi		   PGUID 11 f t t 2 f 701 "701 700" 100 0 0 100  float84mi - ));
DESCR("subtract");

DATA(insert OID = 287 (  float4eq		   PGUID 11 f t t 2 f 16 "700 700" 100 0 0 100	float4eq - ));
DESCR("equal");
DATA(insert OID = 288 (  float4ne		   PGUID 11 f t t 2 f 16 "700 700" 100 0 0 100	float4ne - ));
DESCR("not equal");
DATA(insert OID = 289 (  float4lt		   PGUID 11 f t t 2 f 16 "700 700" 100 0 0 100	float4lt - ));
DESCR("less-than");
DATA(insert OID = 290 (  float4le		   PGUID 11 f t t 2 f 16 "700 700" 100 0 0 100	float4le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 291 (  float4gt		   PGUID 11 f t t 2 f 16 "700 700" 100 0 0 100	float4gt - ));
DESCR("greater-than");
DATA(insert OID = 292 (  float4ge		   PGUID 11 f t t 2 f 16 "700 700" 100 0 0 100	float4ge - ));
DESCR("greater-than-or-equal");

DATA(insert OID = 293 (  float8eq		   PGUID 11 f t t 2 f 16 "701 701" 100 0 0 100	float8eq - ));
DESCR("equal");
DATA(insert OID = 294 (  float8ne		   PGUID 11 f t t 2 f 16 "701 701" 100 0 0 100	float8ne - ));
DESCR("not equal");
DATA(insert OID = 295 (  float8lt		   PGUID 11 f t t 2 f 16 "701 701" 100 0 0 100	float8lt - ));
DESCR("less-than");
DATA(insert OID = 296 (  float8le		   PGUID 11 f t t 2 f 16 "701 701" 100 0 0 100	float8le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 297 (  float8gt		   PGUID 11 f t t 2 f 16 "701 701" 100 0 0 100	float8gt - ));
DESCR("greater-than");
DATA(insert OID = 298 (  float8ge		   PGUID 11 f t t 2 f 16 "701 701" 100 0 0 100	float8ge - ));
DESCR("greater-than-or-equal");

DATA(insert OID = 299 (  float48eq		   PGUID 11 f t t 2 f 16 "700 701" 100 0 0 100	float48eq - ));
DESCR("equal");

/* OIDS 300 - 399 */

DATA(insert OID = 300 (  float48ne		   PGUID 11 f t t 2 f 16 "700 701" 100 0 0 100	float48ne - ));
DESCR("not equal");
DATA(insert OID = 301 (  float48lt		   PGUID 11 f t t 2 f 16 "700 701" 100 0 0 100	float48lt - ));
DESCR("less-than");
DATA(insert OID = 302 (  float48le		   PGUID 11 f t t 2 f 16 "700 701" 100 0 0 100	float48le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 303 (  float48gt		   PGUID 11 f t t 2 f 16 "700 701" 100 0 0 100	float48gt - ));
DESCR("greater-than");
DATA(insert OID = 304 (  float48ge		   PGUID 11 f t t 2 f 16 "700 701" 100 0 0 100	float48ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 305 (  float84eq		   PGUID 11 f t t 2 f 16 "701 700" 100 0 0 100	float84eq - ));
DESCR("equal");
DATA(insert OID = 306 (  float84ne		   PGUID 11 f t t 2 f 16 "701 700" 100 0 0 100	float84ne - ));
DESCR("not equal");
DATA(insert OID = 307 (  float84lt		   PGUID 11 f t t 2 f 16 "701 700" 100 0 0 100	float84lt - ));
DESCR("less-than");
DATA(insert OID = 308 (  float84le		   PGUID 11 f t t 2 f 16 "701 700" 100 0 0 100	float84le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 309 (  float84gt		   PGUID 11 f t t 2 f 16 "701 700" 100 0 0 100	float84gt - ));
DESCR("greater-than");
DATA(insert OID = 310 (  float84ge		   PGUID 11 f t t 2 f 16 "701 700" 100 0 0 100	float84ge - ));
DESCR("greater-than-or-equal");

DATA(insert OID = 311 (  float8			   PGUID 11 f t t 1 f 701 "700" 100 0 0 100  ftod - ));
DESCR("convert float4 to float8");
DATA(insert OID = 312 (  float4			   PGUID 11 f t t 1 f 700 "701" 100 0 0 100  dtof - ));
DESCR("convert float8 to float4");
DATA(insert OID = 313 (  int4			   PGUID 11 f t t 1 f  23  "21" 100 0 0 100  i2toi4 - ));
DESCR("convert int2 to int4");
DATA(insert OID = 314 (  int2			   PGUID 11 f t t 1 f  21  "23" 100 0 0 100  i4toi2 - ));
DESCR("convert int4 to int2");
DATA(insert OID = 315 (  int2vectoreq	   PGUID 11 f t t 2 f  16  "22 22" 100 0 0 100	int2vectoreq - ));
DESCR("equal");
DATA(insert OID = 316 (  float8			   PGUID 11 f t t 1 f 701  "23" 100 0 0 100  i4tod - ));
DESCR("convert int4 to float8");
DATA(insert OID = 317 (  int4			   PGUID 11 f t t 1 f  23 "701" 100 0 0 100  dtoi4 - ));
DESCR("convert float8 to int4");
DATA(insert OID = 318 (  float4			   PGUID 11 f t t 1 f 700  "23" 100 0 0 100  i4tof - ));
DESCR("convert int4 to float4");
DATA(insert OID = 319 (  int4			   PGUID 11 f t t 1 f  23 "700" 100 0 0 100  ftoi4 - ));
DESCR("convert float4 to int4");
#ifdef USEGEO
DATA(insert OID = 320 (  rtinsert		   PGUID 11 f t f 5 f 23 "0" 100 0 0 100  rtinsert - ));
DESCR("r-tree(internal)");
DATA(insert OID = 321 (  rtdelete		   PGUID 11 f t f 2 f 23 "0" 100 0 0 100  rtdelete - ));
DESCR("r-tree(internal)");
DATA(insert OID = 322 (  rtgettuple		   PGUID 11 f t f 2 f 23 "0" 100 0 0 100  rtgettuple - ));
DESCR("r-tree(internal)");
DATA(insert OID = 323 (  rtbuild		   PGUID 11 f t f 9 f 23 "0" 100 0 0 100  rtbuild - ));
DESCR("r-tree(internal)");
DATA(insert OID = 324 (  rtbeginscan	   PGUID 11 f t f 4 f 23 "0" 100 0 0 100  rtbeginscan - ));
DESCR("r-tree(internal)");
DATA(insert OID = 325 (  rtendscan		   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  rtendscan - ));
DESCR("r-tree(internal)");
DATA(insert OID = 326 (  rtmarkpos		   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  rtmarkpos - ));
DESCR("r-tree(internal)");
DATA(insert OID = 327 (  rtrestrpos		   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  rtrestrpos - ));
DESCR("r-tree(internal)");
DATA(insert OID = 328 (  rtrescan		   PGUID 11 f t f 3 f 23 "0" 100 0 0 100  rtrescan - ));
DESCR("r-tree(internal)");
#endif /*  USEGEO  */
DATA(insert OID = 1842 (  btrecoverpage		   PGUID 11 f t f 2 f 23 "0" 100 0 0 100  btrecoverpage - ));
DESCR("btree(internal)");
DATA(insert OID = 330 (  btgettuple		   PGUID 11 f t f 2 f 23 "0" 100 0 0 100  btgettuple - ));
DESCR("btree(internal)");
DATA(insert OID = 331 (  btinsert		   PGUID 11 f t f 5 f 23 "0" 100 0 0 100  btinsert - ));
DESCR("btree(internal)");
DATA(insert OID = 332 (  btbulkdelete		   PGUID 11 f t f 3 f 23 "0" 100 0 0 100  btbulkdelete - ));
DESCR("btree(internal)");
DATA(insert OID = 333 (  btbeginscan	   PGUID 11 f t f 4 f 23 "0" 100 0 0 100  btbeginscan - ));
DESCR("btree(internal)");
DATA(insert OID = 334 (  btrescan		   PGUID 11 f t f 3 f 23 "0" 100 0 0 100  btrescan - ));
DESCR("btree(internal)");
DATA(insert OID = 335 (  btendscan		   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  btendscan - ));
DESCR("btree(internal)");
DATA(insert OID = 336 (  btmarkpos		   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  btmarkpos - ));
DESCR("btree(internal)");
DATA(insert OID = 337 (  btrestrpos		   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  btrestrpos - ));
DESCR("btree(internal)");
DATA(insert OID = 338 (  btbuild		   PGUID 11 f t f 9 f 23 "0" 100 0 0 100  btbuild - ));
DESCR("btree(internal)");
#ifdef USEGEO
DATA(insert OID = 339 (  poly_same		   PGUID 11 f t t 2 f 16 "604 604" 100 0 1 0  poly_same - ));
DESCR("same as");
DATA(insert OID = 340 (  poly_contain	   PGUID 11 f t t 2 f 16 "604 604" 100 0 1 0  poly_contain - ));
DESCR("contains");
DATA(insert OID = 341 (  poly_left		   PGUID 11 f t t 2 f 16 "604 604" 100 0 1 0  poly_left - ));
DESCR("is left of");
DATA(insert OID = 342 (  poly_overleft	   PGUID 11 f t t 2 f 16 "604 604" 100 0 1 0  poly_overleft - ));
DESCR("overlaps, but does not extend to right of");
DATA(insert OID = 343 (  poly_overright    PGUID 11 f t t 2 f 16 "604 604" 100 0 1 0  poly_overright - ));
DESCR("overlaps, but does not extend to left of");
DATA(insert OID = 344 (  poly_right		   PGUID 11 f t t 2 f 16 "604 604" 100 0 1 0  poly_right - ));
DESCR("is left of");
DATA(insert OID = 345 (  poly_contained    PGUID 11 f t t 2 f 16 "604 604" 100 0 1 0  poly_contained - ));
DESCR("contained in");
DATA(insert OID = 346 (  poly_overlap	   PGUID 11 f t t 2 f 16 "604 604" 100 0 1 0  poly_overlap - ));
DESCR("overlaps");
DATA(insert OID = 347 (  poly_in		   PGUID 11 f t t 1 f 604 "0" 100 0 1 0  poly_in - ));
DESCR("(internal)");
DATA(insert OID = 348 (  poly_out		   PGUID 11 f t t 1 f 23  "0" 100 0 1 0  poly_out - ));
DESCR("(internal)");
#endif /* USEGEO */
DATA(insert OID = 350 (  btint2cmp		   PGUID 11 f t t 2 f 23 "21 21" 100 0 0 100  btint2cmp - ));
DESCR("btree less-equal-greater");
DATA(insert OID = 351 (  btint4cmp		   PGUID 11 f t t 2 f 23 "23 23" 100 0 0 100  btint4cmp - ));
DESCR("btree less-equal-greater");
DATA(insert OID = 1137 (  btconnectorcmp		   PGUID 11 f t t 2 f 23 "1136 1136" 100 0 0 100  btconnectorcmp - ));
DESCR("btree less-equal-greater");
DATA(insert OID = 842 (  btint8cmp		   PGUID 11 f t t 2 f 23 "20 20" 100 0 0 100  btint8cmp - ));
DESCR("btree less-equal-greater");
DATA(insert OID = 352 (  btint42cmp		   PGUID 11 f t t 2 f 23 "23 21" 100 0 0 100  btint42cmp - ));
DESCR("btree less-equal-greater");
DATA(insert OID = 353 (  btint24cmp		   PGUID 11 f t t 2 f 23 "21 23" 100 0 0 100  btint24cmp - ));
DESCR("btree less-equal-greater");
DATA(insert OID = 354 (  btfloat4cmp	   PGUID 11 f t t 2 f 23 "700 700" 100 0 0 100	btfloat4cmp - ));
DESCR("btree less-equal-greater");
DATA(insert OID = 355 (  btfloat8cmp	   PGUID 11 f t t 2 f 23 "701 701" 100 0 0 100	btfloat8cmp - ));
DESCR("btree less-equal-greater");
DATA(insert OID = 356 (  btoidcmp		   PGUID 11 f t t 2 f 23 "26 26" 100 0 0 100  btoidcmp - ));
DESCR("btree less-equal-greater");
DATA(insert OID = 404 (  btoidvectorcmp    PGUID 11 f t t 2 f 23 "30 30" 100 0 0 100  btoidvectorcmp - ));
DESCR("btree less-equal-greater");
DATA(insert OID = 357 (  btabstimecmp	   PGUID 11 f t f 2 f 23 "702 702" 100 0 0 100	btabstimecmp - ));
DESCR("btree less-equal-greater");
DATA(insert OID = 358 (  btcharcmp		   PGUID 11 f t t 2 f 23 "18 18" 100 0 0 100  btcharcmp - ));
DESCR("btree less-equal-greater");
DATA(insert OID = 359 (  btnamecmp		   PGUID 11 f t t 2 f 23 "19 19" 100 0 0 100  btnamecmp - ));
DESCR("btree less-equal-greater");
DATA(insert OID = 360 (  bttextcmp		   PGUID 11 f t t 2 f 23 "25 25" 100 0 0 100  bttextcmp - ));
DESCR("btree less-equal-greater");
#ifdef USEGEO
DATA(insert OID = 361 (  lseg_distance	   PGUID 11 f t t 2 f 701 "601 601" 100 0 0 100  lseg_distance - ));
DESCR("distance between");
DATA(insert OID = 362 (  lseg_interpt	   PGUID 11 f t t 2 f 600 "601 601" 100 0 0 100  lseg_interpt - ));
DESCR("");
DATA(insert OID = 363 (  dist_ps		   PGUID 11 f t t 2 f 701 "600 601" 100 0 0 100  dist_ps - ));
DESCR("distance between");
DATA(insert OID = 364 (  dist_pb		   PGUID 11 f t t 2 f 701 "600 603" 100 0 0 100  dist_pb - ));
DESCR("distance between point and box");
DATA(insert OID = 365 (  dist_sb		   PGUID 11 f t t 2 f 701 "601 603" 100 0 0 100  dist_sb - ));
DESCR("distance between segment and box");
DATA(insert OID = 366 (  close_ps		   PGUID 11 f t t 2 f 600 "600 601" 100 0 0 100  close_ps - ));
DESCR("closest point on line segment");
DATA(insert OID = 367 (  close_pb		   PGUID 11 f t t 2 f 600 "600 603" 100 0 0 100  close_pb - ));
DESCR("closest point on box");
DATA(insert OID = 368 (  close_sb		   PGUID 11 f t t 2 f 600 "601 603" 100 0 0 100  close_sb - ));
DESCR("closest point to line segment on box");
DATA(insert OID = 369 (  on_ps			   PGUID 11 f t t 2 f 16 "600 601" 100 0 0 100	on_ps - ));
DESCR("point contained in segment");
DATA(insert OID = 370 (  path_distance	   PGUID 11 f t t 2 f 701 "602 602" 100 0 1 0  path_distance - ));
DESCR("distance between paths");
DATA(insert OID = 371 (  dist_ppath		   PGUID 11 f t t 2 f 701 "600 602" 100 0 1 0  dist_ppath - ));
DESCR("distance between point and patch");
DATA(insert OID = 372 (  on_sb			   PGUID 11 f t t 2 f 16 "601 603" 100 0 0 100	on_sb - ));
DESCR("contained in");
DATA(insert OID = 373 (  inter_sb		   PGUID 11 f t t 2 f 16 "601 603" 100 0 0 100	inter_sb - ));
DESCR("intersects?");
#endif
/* OIDS 400 - 499 */

DATA(insert OID =  406 (  text			   PGUID 11 f t t 1 f	25 "19" 100 0 0 100 name_text - ));
DESCR("convert name to text");
DATA(insert OID =  407 (  name			   PGUID 11 f t t 1 f	19 "25" 100 0 0 100 text_name - ));
DESCR("convert text to name");
DATA(insert OID =  408 (  bpchar		   PGUID 11 f t t 1 f 1042 "19" 100 0 0 100 name_bpchar - ));
DESCR("convert name to char()");
DATA(insert OID =  409 (  name			   PGUID 11 f t t 1 f	19 "1042" 100 0 0 100	bpchar_name - ));
DESCR("convert char() to name");
DATA(insert OID =  402 (  md5			   PGUID 11 f t t 1 f	17 "0" 100 0 0 100	md5 - ));
DESCR("md5 on unknown type");
DATA(insert OID =  1515 (  md5			   PGUID 11 f t t 1 f	17 "1803" 100 0 0 100	md5 - ));
DESCR("md5 on blob");
DATA(insert OID =  1516 (  md5			   PGUID 11 f t t 1 f	17 "25" 100 0 0 100	md5 - ));
DESCR("md5 on text");
DATA(insert OID =  1517 (  md5			   PGUID 11 f t t 1 f	17 "1403" 100 0 0 100	md5 - ));
DESCR("md5 on varchar");
DATA(insert OID =  1518 (  md5			   PGUID 11 f t t 1 f	17 "17" 100 0 0 100	md5 - ));
DESCR("md5 on bytea");
DATA(insert OID =  1519 (  sha2			   PGUID 11 f t t 1 f	17 "0" 100 0 0 100	sha2 - ));
DESCR("sha2 on unknown type");
DATA(insert OID =  1520 (  sha2			   PGUID 11 f t t 1 f	17 "1803" 100 0 0 100	sha2 - ));
DESCR("sha2 on blob");
DATA(insert OID =  1521 (  sha2			   PGUID 11 f t t 1 f	17 "25" 100 0 0 100	sha2 - ));
DESCR("sha2 on text");
DATA(insert OID =  1522 (  sha2			   PGUID 11 f t t 1 f	17 "1403" 100 0 0 100	sha2 - ));
DESCR("sha2 on varchar");
DATA(insert OID =  1523 (  sha2			   PGUID 11 f t t 1 f	17 "17" 100 0 0 100	sha2 - ));
DESCR("sha2 on bytea");

DATA(insert OID =  438 (  hashcostestimate PGUID 11 f t f 7 f 0 "0 0 0 0 0 0 0" 100 0 0 100  hashcostestimate - ));
DESCR("hash index cost estimator");

DATA(insert OID = 440 (  hashgettuple	   PGUID 11 f t f 2 f 23 "0" 100 0 0 100  hashgettuple - ));
DESCR("hash(internal)");
DATA(insert OID = 441 (  hashinsert		   PGUID 11 f t f 5 f 23 "0" 100 0 0 100  hashinsert - ));
DESCR("hash(internal)");
DATA(insert OID = 442 (  hashdelete		   PGUID 11 f t f 2 f 23 "0" 100 0 0 100  hashdelete - ));
DESCR("hash(internal)");
DATA(insert OID = 443 (  hashbeginscan	   PGUID 11 f t f 4 f 23 "0" 100 0 0 100  hashbeginscan - ));
DESCR("hash(internal)");
DATA(insert OID = 444 (  hashrescan		   PGUID 11 f t f 3 f 23 "0" 100 0 0 100  hashrescan - ));
DESCR("hash(internal)");
DATA(insert OID = 445 (  hashendscan	   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  hashendscan - ));
DESCR("hash(internal)");
DATA(insert OID = 446 (  hashmarkpos	   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  hashmarkpos - ));
DESCR("hash(internal)");
DATA(insert OID = 447 (  hashrestrpos	   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  hashrestrpos - ));
DESCR("hash(internal)");
DATA(insert OID = 448 (  hashbuild		   PGUID 11 f t f 9 f 23 "0" 100 0 0 100  hashbuild - ));
DESCR("hash(internal)");
DATA(insert OID = 449 (  hashint2		   PGUID 11 f t t 1 f 23 "21" 100 0 0 100  hashint2 - ));
DESCR("hash");
DATA(insert OID = 450 (  hashint4		   PGUID 11 f t t 1 f 23 "23" 100 0 0 100  hashint4 - ));
DESCR("hash");
DATA(insert OID = 949 (  hashint8		   PGUID 11 f t t 1 f 23 "20" 100 0 0 100  hashint8 - ));
DESCR("hash");
DATA(insert OID = 451 (  hashfloat4		   PGUID 11 f t t 1 f 23 "700" 100 0 0 100	hashfloat4 - ));
DESCR("hash");
DATA(insert OID = 452 (  hashfloat8		   PGUID 11 f t t 1 f 23 "701" 100 0 0 100	hashfloat8 - ));
DESCR("hash");
DATA(insert OID = 453 (  hashoid		   PGUID 11 f t t 1 f 23 "26" 100 0 0 100  hashoid - ));
DESCR("hash");
DATA(insert OID = 454 (  hashchar		   PGUID 11 f t t 1 f 23 "18" 100 0 0 100  hashchar - ));
DESCR("hash");
DATA(insert OID = 455 (  hashname		   PGUID 11 f t t 1 f 23 "19" 100 0 0 100  hashname - ));
DESCR("hash");
DATA(insert OID = 456 (  hashtext		   PGUID 11 f t t 1 f 23 "25" 100 0 0 100  hashtext - ));
DESCR("hash");
DATA(insert OID = 457 (  hashoidvector	   PGUID 11 f t t 1 f 23 "30" 100 0 0 100  hashoidvector - ));
DESCR("hash");

DATA(insert OID = 458 (  text_larger	   PGUID 11 f t t 2 f 25 "25 25" 100 0 0 100  text_larger - ));
DESCR("larger of two");
DATA(insert OID = 459 (  text_smaller	   PGUID 11 f t t 2 f 25 "25 25" 100 0 0 100  text_smaller - ));
DESCR("smaller of two");

DATA(insert OID = 460 (  int8in			   PGUID 11 f t t 1 f 20 "0" 100 0 0 100  int8in - ));
DESCR("(internal)");
DATA(insert OID = 461 (  int8out		   PGUID 11 f t t 1 f 23 "0" 100 0 0 100  int8out - ));
DESCR("(internal)");
DATA(insert OID = 462 (  int8um			   PGUID 11 f t t 1 f 20 "20" 100 0 0 100  int8um - ));
DESCR("negate");
DATA(insert OID = 463 (  int8pl			   PGUID 11 f t t 2 f 20 "20 20" 100 0 0 100  int8pl - ));
DESCR("addition");
DATA(insert OID = 464 (  int8mi			   PGUID 11 f t t 2 f 20 "20 20" 100 0 0 100  int8mi - ));
DESCR("subtraction");
DATA(insert OID = 465 (  int8mul		   PGUID 11 f t t 2 f 20 "20 20" 100 0 0 100  int8mul - ));
DESCR("multiply");
DATA(insert OID = 466 (  int8div		   PGUID 11 f t t 2 f 20 "20 20" 100 0 0 100  int8div - ));
DESCR("divide");
DATA(insert OID = 467 (  int8eq			   PGUID 11 f t t 2 f 16 "20 20" 100 0 0 100  int8eq - ));
DESCR("equal");
DATA(insert OID = 468 (  int8ne			   PGUID 11 f t t 2 f 16 "20 20" 100 0 0 100  int8ne - ));
DESCR("not equal");
DATA(insert OID = 469 (  int8lt			   PGUID 11 f t t 2 f 16 "20 20" 100 0 0 100  int8lt - ));
DESCR("less-than");
DATA(insert OID = 470 (  int8gt			   PGUID 11 f t t 2 f 16 "20 20" 100 0 0 100  int8gt - ));
DESCR("greater-than");
DATA(insert OID = 471 (  int8le			   PGUID 11 f t t 2 f 16 "20 20" 100 0 0 100  int8le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 472 (  int8ge			   PGUID 11 f t t 2 f 16 "20 20" 100 0 0 100  int8ge - ));
DESCR("greater-than-or-equal");

DATA(insert OID = 474 (  int84eq		   PGUID 11 f t t 2 f 16 "20 23" 100 0 0 100  int84eq - ));
DESCR("equal");
DATA(insert OID = 475 (  int84ne		   PGUID 11 f t t 2 f 16 "20 23" 100 0 0 100  int84ne - ));
DESCR("not equal");
DATA(insert OID = 476 (  int84lt		   PGUID 11 f t t 2 f 16 "20 23" 100 0 0 100  int84lt - ));
DESCR("less-than");
DATA(insert OID = 477 (  int84gt		   PGUID 11 f t t 2 f 16 "20 23" 100 0 0 100  int84gt - ));
DESCR("greater-than");
DATA(insert OID = 478 (  int84le		   PGUID 11 f t t 2 f 16 "20 23" 100 0 0 100  int84le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 479 (  int84ge		   PGUID 11 f t t 2 f 16 "20 23" 100 0 0 100  int84ge - ));
DESCR("greater-than-or-equal");

DATA(insert OID = 480 (  int4			   PGUID 11 f t t 1 f  23 "20" 100 0 0 100	int84 - ));
DESCR("convert int8 to int4");
DATA(insert OID = 481 (  int8			   PGUID 11 f t t 1 f  20 "23" 100 0 0 100	int48 - ));
DESCR("convert int4 to int8");
DATA(insert OID = 482 (  float8			   PGUID 11 f t t 1 f 701 "20" 100 0 0 100	i8tod - ));
DESCR("convert int8 to float8");
DATA(insert OID = 483 (  int8			   PGUID 11 f t t 1 f  20 "701" 100 0 0 100  dtoi8 - ));
DESCR("convert float8 to int8");

/* OIDS 500 - 599 */

/* OIDS 600 - 699 */

DATA(insert OID = 1285 (  int4notin		   PGUID 11 f t f 2 f 16 "23 0" 100 0 0 100  int4notin - ));
DESCR("not in");
DATA(insert OID = 1286 (  oidnotin		   PGUID 11 f t f 2 f 16 "26 0" 100 0 0 100  oidnotin - ));
DESCR("not in");
DATA(insert OID = 1287 (  int44in		   PGUID 11 f t t 1 f 22 "0" 100 0 0 100  int44in - ));
DESCR("(internal)");
DATA(insert OID = 653 (  int44out		   PGUID 11 f t t 1 f 23 "0" 100 0 0 100  int44out - ));
DESCR("(internal)");
DATA(insert OID = 655 (  namelt			   PGUID 11 f t t 2 f 16 "19 19" 100 0 0 100  namelt - ));
DESCR("less-than");
DATA(insert OID = 656 (  namele			   PGUID 11 f t t 2 f 16 "19 19" 100 0 0 100  namele - ));
DESCR("less-than-or-equal");
DATA(insert OID = 657 (  namegt			   PGUID 11 f t t 2 f 16 "19 19" 100 0 0 100  namegt - ));
DESCR("greater-than");
DATA(insert OID = 658 (  namege			   PGUID 11 f t t 2 f 16 "19 19" 100 0 0 100  namege - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 659 (  namene			   PGUID 11 f t t 2 f 16 "19 19" 100 0 0 100  namene - ));
DESCR("not equal");

DATA(insert OID = 668 (  bpchar			   PGUID 11 f t t 2 f 1042 "1042 23" 100 0 0 100  bpchar - ));
DESCR("truncate char()");
DATA(insert OID = 669 (  varchar		   PGUID 11 f t t 2 f 1043 "1043 23" 100 0 0 100  varchar - ));
DESCR("truncate varchar()");

DATA(insert OID = 676 (  mktinterval	   PGUID 11 f t f 2 f 704 "702 702" 100 0 0 100 mktinterval - ));
DESCR("convert to tinterval");
DATA(insert OID = 619 (  oidvectorne	   PGUID 11 f t t 2 f 16 "30 30" 100 0 0 100  oidvectorne - ));
DESCR("less-than");
DATA(insert OID = 677 (  oidvectorlt	   PGUID 11 f t t 2 f 16 "30 30" 100 0 0 100  oidvectorlt - ));
DESCR("less-than");
DATA(insert OID = 678 (  oidvectorle	   PGUID 11 f t t 2 f 16 "30 30" 100 0 0 100  oidvectorle - ));
DESCR("less-than-or-equal");
DATA(insert OID = 679 (  oidvectoreq	   PGUID 11 f t t 2 f 16 "30 30" 100 0 0 100  oidvectoreq - ));
DESCR("equal");
DATA(insert OID = 680 (  oidvectorge	   PGUID 11 f t t 2 f 16 "30 30" 100 0 0 100  oidvectorge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 681 (  oidvectorgt	   PGUID 11 f t t 2 f 16 "30 30" 100 0 0 100  oidvectorgt - ));
DESCR("greater-than");

/* OIDS 700 - 799 */
DATA(insert OID = 710 (  getpgusername	   PGUID 11 f t f 0 f 19 "0" 100 0 0 100  getpgusername - ));
DESCR("Return username");
DATA(insert OID = 711 (  userfntest		   PGUID 11 f t t 1 f 23 "23" 100 0 0 100  userfntest - ));
DESCR("");
DATA(insert OID = 713 (  oidrand		   PGUID 11 f t f 2 f 16 "26 23" 100 0 0 100  oidrand - ));
DESCR("random");
DATA(insert OID = 715 (  oidsrand		   PGUID 11 f t f 1 f 16 "23" 100 0 0 100  oidsrand - ));
DESCR("seed random number generator");
DATA(insert OID = 716 (  oideqint4		   PGUID 11 f t t 2 f 16 "26 23" 100 0 0 100  oideqint4 - ));
DESCR("equal");
DATA(insert OID = 717 (  int4eqoid		   PGUID 11 f t t 2 f 16 "23 26" 100 0 0 100  int4eqoid - ));
DESCR("equal");

DATA(insert OID = 720 (  octet_length	   PGUID 11 f t t 1 f 23 "17" 100 0 0 100  byteaoctetlen - ));
DESCR("");
DATA(insert OID = 721 (  get_byte		   PGUID 11 f t t 2 f 23 "17 23" 100 0 0 100  byteaGetByte - ));
DESCR("");
DATA(insert OID = 722 (  set_byte		   PGUID 11 f t t 3 f 17 "17 23 23" 100 0 0 100  byteaSetByte - ));
DESCR("");
DATA(insert OID = 723 (  get_bit		   PGUID 11 f t t 2 f 23 "17 23" 100 0 0 100  byteaGetBit - ));
DESCR("");
DATA(insert OID = 724 (  set_bit		   PGUID 11 f t t 3 f 17 "17 23 23" 100 0 0 100  byteaSetBit - ));
DESCR("");
#ifdef USEGEO
DATA(insert OID = 725 (  dist_pl		   PGUID 11 f t t 2 f 701 "600 628" 100 0 0 100  dist_pl - ));
DESCR("distance between point and line");
DATA(insert OID = 726 (  dist_lb		   PGUID 11 f t t 2 f 701 "628 603" 100 0 0 100  dist_lb - ));
DESCR("distance between line and box");
DATA(insert OID = 727 (  dist_sl		   PGUID 11 f t t 2 f 701 "601 628" 100 0 0 100  dist_sl - ));
DESCR("distance between lseg and line");
DATA(insert OID = 728 (  dist_cpoly		   PGUID 11 f t t 2 f 701 "718 604" 100 0 0 100  dist_cpoly - ));
DESCR("distance between");
DATA(insert OID = 729 (  poly_distance	   PGUID 11 f t t 2 f 701 "604 604" 100 0 0 100  poly_distance - ));
DESCR("distance between");
#endif
DATA(insert OID = 730 (  pqtest			   PGUID 11 f t f 1 f 23 "25" 100 0 0 100  pqtest - ));
DESCR("");

DATA(insert OID = 740 (  text_lt		   PGUID 11 f t t 2 f 16 "25 25" 100 0 0 0	text_lt - ));
DESCR("less-than");
DATA(insert OID = 741 (  text_le		   PGUID 11 f t t 2 f 16 "25 25" 100 0 0 0	text_le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 742 (  text_gt		   PGUID 11 f t t 2 f 16 "25 25" 100 0 0 0	text_gt - ));
DESCR("greater-than");
DATA(insert OID = 743 (  text_ge		   PGUID 11 f t t 2 f 16 "25 25" 100 0 0 0	text_ge - ));
DESCR("greater-than-or-equal");

#ifndef NOARRAY
DATA(insert OID = 744 (  array_eq		   PGUID 11 f t t 2 f 16 "0 0" 100 0 0 100 array_eq -));
DESCR("equal");
DATA(insert OID = 745 (  array_assgn	   PGUID 11 f t t 8 f 23 "0 23 0 0 0 23 23 0" 100 0 0 100 array_assgn -));
DESCR("array");
DATA(insert OID = 746 (  array_clip		   PGUID 11 f t t 7 f 23 "0 23 0 0 23 23 0" 100 0 0 100 array_clip -));
DESCR("array");
DATA(insert OID = 747 (  array_dims		   PGUID 11 f t t 1 f 25 "0" 100 0 0 100 array_dims -));
DESCR("array(internal)");
DATA(insert OID = 748 (  array_set		   PGUID 11 f t t 8 f 23 "0 23 0 0 23 23 23 0" 100 0 0 100 array_set -));
DESCR("array");
DATA(insert OID = 749 (  array_ref		   PGUID 11 f t t 7 f 23 "0 23 0 23 23 23 0" 100 0 0 100 array_ref -));
DESCR("array");
DATA(insert OID = 750 (  array_in		   PGUID 11 f t t 3 f 23 "0 0 23" 100 0 0 100	array_in - ));
DESCR("array");
DATA(insert OID = 751 (  array_out		   PGUID 11 f t t 2 f 23 "0 0" 100 0 0 100	array_out - ));
DESCR("array");
#endif

DATA(insert OID = 752 (  filename_in	   PGUID 11 f t t 1 f 605 "0" 100 0 0 100  filename_in - ));
DESCR("(internal)");
DATA(insert OID = 753 (  filename_out	   PGUID 11 f t t 2 f 19  "0 0" 100 0 0 100  filename_out - ));
DESCR("(internal)");

DATA(insert OID = 760 (  smgrin			   PGUID 11 f t f 1 f 210 "0" 100 0 0 100  smgrin - ));
DESCR("storage manager(internal)");
DATA(insert OID = 761 (  smgrout		   PGUID 11 f t f 1 f 23  "0" 100 0 0 100  smgrout - ));
DESCR("storage manager(internal)");
DATA(insert OID = 762 (  smgreq			   PGUID 11 f t f 2 f 16 "210 210" 100 0 0 100	smgreq - ));
DESCR("storage manager");
DATA(insert OID = 763 (  smgrne			   PGUID 11 f t f 2 f 16 "210 210" 100 0 0 100	smgrne - ));
DESCR("storage manager");

DATA(insert OID = 766 (  int4inc		   PGUID 11 f t t 1 f 23 "23" 100 0 0 100  int4inc - ));
DESCR("increment");
DATA(insert OID = 767 (  int2inc		   PGUID 11 f t t 1 f 21 "21" 100 0 0 100  int2inc - ));
DESCR("increment");
DATA(insert OID = 768 (  int4larger		   PGUID 11 f t t 2 f 23 "23 23" 100 0 0 100  int4larger - ));
DESCR("larger of two");
DATA(insert OID = 769 (  int4smaller	   PGUID 11 f t t 2 f 23 "23 23" 100 0 0 100  int4smaller - ));
DESCR("smaller of two");
DATA(insert OID = 770 (  int2larger		   PGUID 11 f t t 2 f 21 "21 21" 100 0 0 100  int2larger - ));
DESCR("larger of two");
DATA(insert OID = 771 (  int2smaller	   PGUID 11 f t t 2 f 21 "21 21" 100 0 0 100  int2smaller - ));
DESCR("smaller of two");
/*
DATA(insert OID = 772 (  gistcostestimate  PGUID 11 f t f 7 f 0 "0 0 0 0 0 0 0" 100 0 0 100  gistcostestimate - ));
DESCR("gist cost estimator");
DATA(insert OID = 774 (  gistgettuple	   PGUID 11 f t f 2 f 23 "0" 100 0 0 100  gistgettuple - ));
DESCR("gist(internal)");
DATA(insert OID = 775 (  gistinsert		   PGUID 11 f t f 5 f 23 "0" 100 0 0 100  gistinsert - ));
DESCR("gist(internal)");
DATA(insert OID = 776 (  gistdelete		   PGUID 11 f t f 2 f 23 "0" 100 0 0 100  gistdelete - ));
DESCR("gist(internal)");
DATA(insert OID = 777 (  gistbeginscan	   PGUID 11 f t f 4 f 23 "0" 100 0 0 100  gistbeginscan - ));
DESCR("gist(internal)");
DATA(insert OID = 778 (  gistrescan		   PGUID 11 f t f 3 f 23 "0" 100 0 0 100  gistrescan - ));
DESCR("gist(internal)");
DATA(insert OID = 779 (  gistendscan	   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  gistendscan - ));
DESCR("gist(internal)");
DATA(insert OID = 780 (  gistmarkpos	   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  gistmarkpos - ));
DESCR("gist(internal)");
DATA(insert OID = 781 (  gistrestrpos	   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  gistrestrpos - ));
DESCR("gist(internal)");
DATA(insert OID = 782 (  gistbuild		   PGUID 11 f t f 9 f 23 "0" 100 0 0 100  gistbuild - ));
DESCR("gist(internal)");
*/
DATA(insert OID = 784 (  tintervaleq	   PGUID 11 f t f 2 f 16 "704 704" 100 0 0 100	tintervaleq - ));
DESCR("equal");
DATA(insert OID = 785 (  tintervalne	   PGUID 11 f t f 2 f 16 "704 704" 100 0 0 100	tintervalne - ));
DESCR("not equal");
DATA(insert OID = 786 (  tintervallt	   PGUID 11 f t f 2 f 16 "704 704" 100 0 0 100	tintervallt - ));
DESCR("less-than");
DATA(insert OID = 787 (  tintervalgt	   PGUID 11 f t f 2 f 16 "704 704" 100 0 0 100	tintervalgt - ));
DESCR("greater-than");
DATA(insert OID = 788 (  tintervalle	   PGUID 11 f t f 2 f 16 "704 704" 100 0 0 100	tintervalle - ));
DESCR("less-than-or-equal");
DATA(insert OID = 789 (  tintervalge	   PGUID 11 f t f 2 f 16 "704 704" 100 0 0 100	tintervalge - ));
DESCR("greater-than-or-equal");

/* OIDS 800 - 899 */

DATA(insert OID = 817 (  oid			   PGUID 11 f t t 1 f 26 "25" 100 0 0 100  text_oid -));
DESCR("convert text to oid");
DATA(insert OID = 818 (  int2			   PGUID 11 f t t 1 f 21 "25" 100 0 0 100  text_int2 -));
DESCR("convert text to int2");
DATA(insert OID = 819 (  int4			   PGUID 11 f t t 1 f 23 "25" 100 0 0 100  text_int4 -));
DESCR("convert text to int4");

DATA(insert OID = 838 (  float8			   PGUID 11 f t t 1 f 701 "25" 100 0 0 100	text_float8 -));
DESCR("convert text to float8");
DATA(insert OID = 839 (  float4			   PGUID 11 f t t 1 f 700 "25" 100 0 0 100	text_float4 -));
DESCR("convert text to float4");
DATA(insert OID = 840 (  text			   PGUID 11 f t t 1 f  25 "701" 100 0 0 100  float8_text -));
DESCR("convert float8 to text");
DATA(insert OID = 841 (  text			   PGUID 11 f t t 1 f  25 "700" 100 0 0 100  float4_text -));
DESCR("convert float4 to text");

DATA(insert OID =  846 (  cash_mul_flt4    PGUID 11 f t t 2 f 790 "790 700" 100 0 0 100  cash_mul_flt4 - ));
DESCR("multiply");
DATA(insert OID =  847 (  cash_div_flt4    PGUID 11 f t t 2 f 790 "790 700" 100 0 0 100  cash_div_flt4 - ));
DESCR("divide");
DATA(insert OID =  848 (  flt4_mul_cash    PGUID 11 f t t 2 f 790 "700 790" 100 0 0 100  flt4_mul_cash - ));
DESCR("multiply");

DATA(insert OID =  849 (  position		   PGUID 11 f t t 2 f 23 "25 25" 100 0 1 0 textpos - ));
DESCR("return position of substring");
DATA(insert OID =  850 (  textlike		   PGUID 11 f t t 2 f 16 "25 25" 100 0 1 0 textlike - ));
DESCR("matches LIKE expression");
DATA(insert OID =  851 (  textnlike		   PGUID 11 f t t 2 f 16 "25 25" 100 0 1 0 textnlike - ));
DESCR("does not match LIKE expression");

DATA(insert OID =  852 (  int48eq		   PGUID 11 f t t 2 f 16 "23 20" 100 0 0 100  int48eq - ));
DESCR("equal");
DATA(insert OID =  853 (  int48ne		   PGUID 11 f t t 2 f 16 "23 20" 100 0 0 100  int48ne - ));
DESCR("not equal");
DATA(insert OID =  854 (  int48lt		   PGUID 11 f t t 2 f 16 "23 20" 100 0 0 100  int48lt - ));
DESCR("less-than");
DATA(insert OID =  855 (  int48gt		   PGUID 11 f t t 2 f 16 "23 20" 100 0 0 100  int48gt - ));
DESCR("greater-than");
DATA(insert OID =  856 (  int48le		   PGUID 11 f t t 2 f 16 "23 20" 100 0 0 100  int48le - ));
DESCR("less-than-or-equal");
DATA(insert OID =  857 (  int48ge		   PGUID 11 f t t 2 f 16 "23 20" 100 0 0 100  int48ge - ));
DESCR("greater-than-or-equal");

DATA(insert OID =  858 (  namelike		   PGUID 11 f t t 2 f 16 "19 25" 100 0 0 100  namelike - ));
DESCR("matches LIKE expression");
DATA(insert OID =  859 (  namenlike		   PGUID 11 f t t 2 f 16 "19 25" 100 0 0 100  namenlike - ));
DESCR("does not match LIKE expression");

DATA(insert OID =  860 (  bpchar		   PGUID 11 f t t 1 f 1042 "18" 100 0 0 100  char_bpchar - ));
DESCR("convert char to char()");
DATA(insert OID =  861 (  char			   PGUID 11 f t t 1 f	18 "1042" 100 0 0 100  bpchar_char - ));
DESCR("convert char() to char");

DATA(insert OID =  862 (  int4_mul_cash		   PGUID 11 f t t 2 f 790 "23 790" 100 0 0 100	int4_mul_cash - ));
DESCR("multiply");
DATA(insert OID =  863 (  int2_mul_cash		   PGUID 11 f t t 2 f 790 "21 790" 100 0 0 100	int2_mul_cash - ));
DESCR("multiply");
DATA(insert OID =  864 (  cash_mul_int4		   PGUID 11 f t t 2 f 790 "790 23" 100 0 0 100	cash_mul_int4 - ));
DESCR("multiply");
DATA(insert OID =  865 (  cash_div_int4		   PGUID 11 f t t 2 f 790 "790 23" 100 0 0 100	cash_div_int4 - ));
DESCR("divide");
DATA(insert OID =  866 (  cash_mul_int2		   PGUID 11 f t t 2 f 790 "790 21" 100 0 0 100	cash_mul_int2 - ));
DESCR("multiply");
DATA(insert OID =  867 (  cash_div_int2		   PGUID 11 f t t 2 f 790 "790 21" 100 0 0 100	cash_div_int2 - ));
DESCR("divide");

DATA(insert OID =  886 (  cash_in		   PGUID 11 f t t 1 f 790 "0" 100 0 0 100  cash_in - ));
DESCR("(internal)");
DATA(insert OID =  887 (  cash_out		   PGUID 11 f t t 1 f  23 "0" 100 0 0 100  cash_out - ));
DESCR("(internal)");
DATA(insert OID =  1273 (  cash_words_out  PGUID 11 f t t 1 f  25 "790" 100 0 0 100  cash_words_out - ));
DESCR("output amount as words");
DATA(insert OID =  888 (  cash_eq		   PGUID 11 f t t 2 f  16 "790 790" 100 0 0 100  cash_eq - ));
DESCR("equal");
DATA(insert OID =  889 (  cash_ne		   PGUID 11 f t t 2 f  16 "790 790" 100 0 0 100  cash_ne - ));
DESCR("not equal");
DATA(insert OID =  890 (  cash_lt		   PGUID 11 f t t 2 f  16 "790 790" 100 0 0 100  cash_lt - ));
DESCR("less-than");
DATA(insert OID =  891 (  cash_le		   PGUID 11 f t t 2 f  16 "790 790" 100 0 0 100  cash_le - ));
DESCR("less-than-or-equal");
DATA(insert OID =  892 (  cash_gt		   PGUID 11 f t t 2 f  16 "790 790" 100 0 0 100  cash_gt - ));
DESCR("greater-than");
DATA(insert OID =  893 (  cash_ge		   PGUID 11 f t t 2 f  16 "790 790" 100 0 0 100  cash_ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID =  894 (  cash_pl		   PGUID 11 f t t 2 f 790 "790 790" 100 0 0 100  cash_pl - ));
DESCR("addition");
DATA(insert OID =  895 (  cash_mi		   PGUID 11 f t t 2 f 790 "790 790" 100 0 0 100  cash_mi - ));
DESCR("subtract");
DATA(insert OID =  896 (  cash_mul_flt8		   PGUID 11 f t t 2 f 790 "790 701" 100 0 0 100  cash_mul_flt8 - ));
DESCR("multiply");
DATA(insert OID =  897 (  cash_div_flt8		   PGUID 11 f t t 2 f 790 "790 701" 100 0 0 100  cash_div_flt8 - ));
DESCR("divide");
DATA(insert OID =  898 (  cashlarger	   PGUID 11 f t t 2 f 790 "790 790" 100 0 0 100  cashlarger - ));
DESCR("larger of two");
DATA(insert OID =  899 (  cashsmaller	   PGUID 11 f t t 2 f 790 "790 790" 100 0 0 100  cashsmaller - ));
DESCR("smaller of two");

DATA(insert OID =  919 (  flt8_mul_cash		   PGUID 11 f t t 2 f 790 "701 790" 100 0 0 100  flt8_mul_cash - ));
DESCR("multiply");

/* OIDS 900 - 999 */

/* isoldpath, upgradepath, upgradepoly, revertpoly are used to update pre-v6.1 to v6.1 - tgl 97/06/03 */
#ifdef USEGEO
DATA(insert OID = 936 (  isoldpath		   PGUID 11 f t f 1 f  16 "602" 100 0 0 100  isoldpath - ));
DESCR("");
DATA(insert OID = 937 (  upgradepath	   PGUID 11 f t f 1 f 602 "602" 100 0 0 100  upgradepath - ));
DESCR("");
DATA(insert OID = 938 (  upgradepoly	   PGUID 11 f t f 1 f 604 "604" 100 0 0 100  upgradepoly - ));
DESCR("");
DATA(insert OID = 939 (  revertpoly		   PGUID 11 f t f 1 f 604 "604" 100 0 0 100  revertpoly - ));
DESCR("");
#endif /*  USEGEO */

DATA(insert OID = 940 (  mod			   PGUID 11 f t t 2 f 21 "21 21" 100 0 0 100  int2mod - ));
DESCR("modulus");
DATA(insert OID = 941 (  mod			   PGUID 11 f t t 2 f 23 "23 23" 100 0 0 100  int4mod - ));
DESCR("modulus");
DATA(insert OID = 942 (  mod			   PGUID 11 f t t 2 f 23 "21 23" 100 0 0 100  int24mod - ));
DESCR("modulus");
DATA(insert OID = 943 (  mod			   PGUID 11 f t t 2 f 23 "23 21" 100 0 0 100  int42mod - ));
DESCR("modulus");

DATA(insert OID = 945 (  int8mod		   PGUID 11 f t t 2 f 20 "20 20" 100 0 0 100  int8mod - ));
DESCR("modulus");
DATA(insert OID = 947 (  mod			   PGUID 11 f t t 2 f 20 "20 20" 100 0 0 100  int8mod - ));
DESCR("modulus");

DATA(insert OID = 944 (  char			   PGUID 11 f t t 1 f 18 "25" 100 0 0 100  text_char - ));
DESCR("convert text to char");
DATA(insert OID = 946 (  text			   PGUID 11 f t t 1 f 25 "18" 100 0 0 100  char_text - ));
DESCR("convert char to text");
DATA(insert OID = 948 (  varchar		   PGUID 11 f t t 1 f 25 "1043" 100 0 0 100  bpchar_char - ));
DESCR("convert varchar() to text");

DATA(insert OID = 950 (  istrue			   PGUID 11 f t t 1 f 16 "16" 100 0 0 100  istrue - ));
DESCR("");
DATA(insert OID = 951 (  isfalse		   PGUID 11 f t t 1 f 16 "16" 100 0 0 100  isfalse - ));
DESCR("");
#ifdef USEGEO
DATA(insert OID = 959 (  on_pl			   PGUID 11 f t t 2 f  16 "600 628" 100 0 10 100  on_pl - ));
DESCR("point on line?");
DATA(insert OID = 960 (  on_sl			   PGUID 11 f t t 2 f  16 "601 628" 100 0 10 100  on_sl - ));
DESCR("lseg on line?");
DATA(insert OID = 961 (  close_pl		   PGUID 11 f t t 2 f 600 "600 628" 100 0 10 100  close_pl - ));
DESCR("closest point on line");
DATA(insert OID = 962 (  close_sl		   PGUID 11 f t t 2 f 600 "601 628" 100 0 10 100  close_sl - ));
DESCR("closest point to line segment on line");
DATA(insert OID = 963 (  close_lb		   PGUID 11 f t t 2 f 600 "628 603" 100 0 10 100  close_lb - ));
DESCR("closest point to line on box");
#endif /* USEGEO */

DATA(insert OID = 972 (  regproctooid	   PGUID 11 f t t 1 f  26 "24" 100 0 0 100	regproctooid - ));
DESCR("get oid for regproc");
#ifdef USEGEO
DATA(insert OID = 973 (  path_inter		   PGUID 11 f t t 2 f  16 "602 602" 100 0 10 100  path_inter - ));
DESCR("paths intersect?");
DATA(insert OID = 975 (  area			   PGUID 11 f t t 1 f 701 "603" 100 0 0 100  rect_area - ));
DESCR("box area");
DATA(insert OID = 976 (  width			   PGUID 11 f t t 1 f 701 "603" 100 0 0 100  rect_width - ));
DESCR("box width");
DATA(insert OID = 977 (  height			   PGUID 11 f t t 1 f 701 "603" 100 0 0 100  rect_height - ));
DESCR("box height");
DATA(insert OID = 978 (  rect_distance	   PGUID 11 f t t 2 f 701 "603 603" 100 0 0 100  rect_distance - ));
DESCR("distance between boxes");
DATA(insert OID = 980 (  rect_intersect	   PGUID 11 f t t 2 f 603 "603 603" 100 0 0 100  rect_intersect - ));
DESCR("box intersection (another box)");
DATA(insert OID = 981 (  diagonal		   PGUID 11 f t t 1 f 601 "603" 100 0 0 100  rect_diagonal - ));
DESCR("box diagonal");
DATA(insert OID = 982 (  path_n_lt		   PGUID 11 f t t 2 f 16 "602 602" 100 0 0 100	path_n_lt - ));
DESCR("less-than");
DATA(insert OID = 983 (  path_n_gt		   PGUID 11 f t t 2 f 16 "602 602" 100 0 0 100	path_n_gt - ));
DESCR("greater-than");
DATA(insert OID = 984 (  path_n_eq		   PGUID 11 f t t 2 f 16 "602 602" 100 0 0 100	path_n_eq - ));
DESCR("equal");
DATA(insert OID = 985 (  path_n_le		   PGUID 11 f t t 2 f 16 "602 602" 100 0 0 100	path_n_le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 986 (  path_n_ge		   PGUID 11 f t t 2 f 16 "602 602" 100 0 0 100	path_n_ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 987 (  path_length	   PGUID 11 f t t 1 f 701 "602" 100 0 1 0  path_length - ));
DESCR("sum of path segments");
DATA(insert OID = 988 (  point_ne		   PGUID 11 f t t 2 f 16 "600 600" 100 0 0 100	point_ne - ));
DESCR("not equal");
DATA(insert OID = 989 (  point_vert		   PGUID 11 f t t 2 f 16 "600 600" 100 0 0 100	point_vert - ));
DESCR("vertical?");
DATA(insert OID = 990 (  point_horiz	   PGUID 11 f t t 2 f 16 "600 600" 100 0 0 100	point_horiz - ));
DESCR("horizontal?");
DATA(insert OID = 991 (  point_distance    PGUID 11 f t t 2 f 701 "600 600" 100 0 0 100  point_distance - ));
DESCR("distance between");
DATA(insert OID = 992 (  slope			   PGUID 11 f t t 2 f 701 "600 600" 100 0 0 100  point_slope - ));
DESCR("slope between points");
DATA(insert OID = 993 (  lseg			   PGUID 11 f t t 2 f 601 "600 600" 100 0 0 100  lseg_construct - ));
DESCR("convert points to line segment");
DATA(insert OID = 994 (  lseg_intersect    PGUID 11 f t t 2 f 16 "601 601" 100 0 0 100	lseg_intersect - ));
DESCR("intersects?");
DATA(insert OID = 995 (  lseg_parallel	   PGUID 11 f t t 2 f 16 "601 601" 100 0 0 100	lseg_parallel - ));
DESCR("parallel?");
DATA(insert OID = 996 (  lseg_perp		   PGUID 11 f t t 2 f 16 "601 601" 100 0 0 100	lseg_perp - ));
DESCR("perpendicular?");
DATA(insert OID = 997 (  lseg_vertical	   PGUID 11 f t t 1 f 16 "601" 100 0 0 100	lseg_vertical - ));
DESCR("vertical?");
DATA(insert OID = 998 (  lseg_horizontal   PGUID 11 f t t 1 f 16 "601" 100 0 0 100	lseg_horizontal - ));
DESCR("horizontal?");
DATA(insert OID = 999 (  lseg_eq		   PGUID 11 f t t 2 f 16 "601 601" 100 0 0 100	lseg_eq - ));
DESCR("equal");
#endif /* USEGEO  */
/* OIDS 1000 - 1999 */

DATA(insert OID = 1029 (  nullvalue		   PGUID 11 f t t 1 f 16 "0" 100 0 0 100  nullvalue - ));
DESCR("(internal)");
DATA(insert OID = 1030 (  nonnullvalue	   PGUID 11 f t t 1 f 16 "0" 100 0 0 100  nonnullvalue - ));
DESCR("(internal)");

#ifndef NOARRAY
DATA(insert OID = 1031 (  aclitemin		   PGUID 11 f t f 1 f 1033 "0" 100 0 0 100	aclitemin - ));
DESCR("(internal)");
DATA(insert OID = 1032 (  aclitemout	   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  aclitemout - ));
DESCR("(internal)");
DATA(insert OID = 1035 (  aclinsert		   PGUID 11 f t f 2 f 1034 "1034 1033" 100 0 0 100	aclinsert - ));
DESCR("addition");
DATA(insert OID = 1036 (  aclremove		   PGUID 11 f t f 2 f 1034 "1034 1033" 100 0 0 100	aclremove - ));
DESCR("subtract");
DATA(insert OID = 1037 (  aclcontains	   PGUID 11 f t f 2 f 16 "1034 1033" 100 0 0 100  aclcontains - ));
DESCR("matches regex., case-sensitive");
#endif

DATA(insert OID = 1038 (  seteval		   PGUID 11 f t f 1 f 23 "26" 100 0 0 100  seteval - ));
DESCR("");
DATA(insert OID = 1044 (  bpcharin		   PGUID 11 f t t 3 f 1042 "0 0 23" 100 0 0 100 bpcharin - ));
DESCR("(internal)");
DATA(insert OID = 1045 (  bpcharout		   PGUID 11 f t t 1 f 23 "0" 100 0 0 100  bpcharout - ));
DESCR("(internal)");
DATA(insert OID = 1046 (  varcharin		   PGUID 11 f t t 3 f 1043 "0 0 23" 100 0 0 100 varcharin - ));
DESCR("(internal)");
DATA(insert OID = 1047 (  varcharout	   PGUID 11 f t t 1 f 23 "0" 100 0 0 100  varcharout - ));
DESCR("(internal)");
DATA(insert OID = 1048 (  bpchareq		   PGUID 11 f t t 2 f 16 "1042 1042" 100 0 0 100  bpchareq - ));
DESCR("equal");
DATA(insert OID = 1049 (  bpcharlt		   PGUID 11 f t t 2 f 16 "1042 1042" 100 0 0 100  bpcharlt - ));
DESCR("less-than");
DATA(insert OID = 1050 (  bpcharle		   PGUID 11 f t t 2 f 16 "1042 1042" 100 0 0 100  bpcharle - ));
DESCR("less-than-or-equal");
DATA(insert OID = 1051 (  bpchargt		   PGUID 11 f t t 2 f 16 "1042 1042" 100 0 0 100  bpchargt - ));
DESCR("greater-than");
DATA(insert OID = 1052 (  bpcharge		   PGUID 11 f t t 2 f 16 "1042 1042" 100 0 0 100  bpcharge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 1053 (  bpcharne		   PGUID 11 f t t 2 f 16 "1042 1042" 100 0 0 100  bpcharne - ));
DESCR("not equal");
DATA(insert OID = 1506 (  byteaeq		   PGUID 11 f t t 2 f 16 "17 17" 100 0 0 100  byteaeq - ));
DESCR("equal");
DATA(insert OID = 1507 (  bytealt		   PGUID 11 f t t 2 f 16 "17 17" 100 0 0 100  bytealt - ));
DESCR("less-than");
DATA(insert OID = 1508 (  byteale		   PGUID 11 f t t 2 f 16 "17 17" 100 0 0 100  byteale - ));
DESCR("less-than-or-equal");
DATA(insert OID = 1509 (  byteagt		   PGUID 11 f t t 2 f 16 "17 17" 100 0 0 100  byteagt - ));
DESCR("greater-than");
DATA(insert OID = 1510 (  byteage		   PGUID 11 f t t 2 f 16 "17 17" 100 0 0 100  byteage - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 1511 (  byteane		   PGUID 11 f t t 2 f 16 "17 17" 100 0 0 100  byteane - ));
DESCR("not equal");
DATA(insert OID = 1070 (  varchareq		   PGUID 11 f t t 2 f 16 "17 17" 100 0 0 100  varchareq - ));
DESCR("equal");
DATA(insert OID = 1071 (  varcharlt		   PGUID 11 f t t 2 f 16 "1043 1043" 100 0 0 100  varcharlt - ));
DESCR("less-than");
DATA(insert OID = 1072 (  varcharle		   PGUID 11 f t t 2 f 16 "1043 1043" 100 0 0 100  varcharle - ));
DESCR("less-than-or-equal");
DATA(insert OID = 1073 (  varchargt		   PGUID 11 f t t 2 f 16 "1043 1043" 100 0 0 100  varchargt - ));
DESCR("greater-than");
DATA(insert OID = 1074 (  varcharge		   PGUID 11 f t t 2 f 16 "1043 1043" 100 0 0 100  varcharge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 1075 (  varcharne		   PGUID 11 f t t 2 f 16 "1043 1043" 100 0 0 100  varcharne - ));
DESCR("not equal");
DATA(insert OID = 1078 (  bpcharcmp		   PGUID 11 f t t 2 f 23 "1042 1042" 100 0 0 100  bpcharcmp - ));
DESCR("less-equal-greater");
DATA(insert OID = 1512 (  byteacmp	   PGUID 11 f t t 2 f 23 "17 17" 100 0 0 100  byteacmp - ));
DESCR("less-equal-greater");
DATA(insert OID = 1079 (  varcharcmp	   PGUID 11 f t t 2 f 23 "1043 1043" 100 0 0 100  varcharcmp - ));
DESCR("less-equal-greater");
DATA(insert OID = 1080 (  hashbpchar	   PGUID 11 f t t 1 f 23 "1042" 100 0 0 100  hashbpchar - ));
DESCR("hash");
DATA(insert OID = 1081 (  hashvarchar	   PGUID 11 f t t 1 f 23 "1043" 100 0 0 100  hashvarchar - ));
DESCR("hash");

DATA(insert OID = 1084 (  date_in		   PGUID 11 f t f 1 f 1082 "0" 100 0 0 100	date_in - ));
DESCR("(internal)");
DATA(insert OID = 1085 (  date_out		   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  date_out - ));
DESCR("(internal)");
DATA(insert OID = 1086 (  date_eq		   PGUID 11 f t t 2 f 16 "1082 1082" 100 0 0 100  date_eq - ));
DESCR("equal");
DATA(insert OID = 1087 (  date_lt		   PGUID 11 f t t 2 f 16 "1082 1082" 100 0 0 100  date_lt - ));
DESCR("less-than");
DATA(insert OID = 1088 (  date_le		   PGUID 11 f t t 2 f 16 "1082 1082" 100 0 0 100  date_le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 1089 (  date_gt		   PGUID 11 f t t 2 f 16 "1082 1082" 100 0 0 100  date_gt - ));
DESCR("greater-than");
DATA(insert OID = 1090 (  date_ge		   PGUID 11 f t t 2 f 16 "1082 1082" 100 0 0 100  date_ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 1091 (  date_ne		   PGUID 11 f t t 2 f 16 "1082 1082" 100 0 0 100  date_ne - ));
DESCR("not equal");
DATA(insert OID = 1092 (  date_cmp		   PGUID 11 f t t 2 f 23 "1082 1082" 100 0 0 100  date_cmp - ));
DESCR("less-equal-greater");

/* OIDS 1100 - 1199 */

DATA(insert OID = 1102 (  time_lt		   PGUID 11 f t t 2 f 16 "1083 1083" 100 0 0 100  time_lt - ));
DESCR("less-than");
DATA(insert OID = 1103 (  time_le		   PGUID 11 f t t 2 f 16 "1083 1083" 100 0 0 100  time_le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 1104 (  time_gt		   PGUID 11 f t t 2 f 16 "1083 1083" 100 0 0 100  time_gt - ));
DESCR("greater-than");
DATA(insert OID = 1105 (  time_ge		   PGUID 11 f t t 2 f 16 "1083 1083" 100 0 0 100  time_ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 1106 (  time_ne		   PGUID 11 f t t 2 f 16 "1083 1083" 100 0 0 100  time_ne - ));
DESCR("not equal");
DATA(insert OID = 1107 (  time_cmp		   PGUID 11 f t t 2 f 23 "1083 1083" 100 0 0 100  time_cmp - ));
DESCR("less-equal-greater");
DATA(insert OID = 1138 (  date_larger	   PGUID 11 f t t 2 f 1082 "1082 1082" 100 0 0 100	date_larger - ));
DESCR("larger of two");
DATA(insert OID = 1139 (  date_smaller	   PGUID 11 f t t 2 f 1082 "1082 1082" 100 0 0 100	date_smaller - ));
DESCR("smaller of two");
DATA(insert OID = 1140 (  date_mi		   PGUID 11 f t t 2 f 23 "1082 1082" 100 0 0 100  date_mi - ));
DESCR("subtract");
DATA(insert OID = 1141 (  date_pli		   PGUID 11 f t t 2 f 1082 "1082 23" 100 0 0 100  date_pli - ));
DESCR("addition");
DATA(insert OID = 1142 (  date_mii		   PGUID 11 f t t 2 f 1082 "1082 23" 100 0 0 100  date_mii - ));
DESCR("subtract");
DATA(insert OID = 1143 (  time_in		   PGUID 11 f t f 1 f 1083 "0" 100 0 0 100	time_in - ));
DESCR("(internal)");
DATA(insert OID = 1144 (  time_out		   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  time_out - ));
DESCR("(internal)");
DATA(insert OID = 1145 (  time_eq		   PGUID 11 f t t 2 f 16 "1083 1083" 100 0 0 100  time_eq - ));
DESCR("equal");
#ifdef USEGEO
DATA(insert OID = 1146 (  circle_add_pt    PGUID 11 f t t 2 f 718 "718 600" 100 0 0 100  circle_add_pt - ));
DESCR("addition");
DATA(insert OID = 1147 (  circle_sub_pt    PGUID 11 f t t 2 f 718 "718 600" 100 0 0 100  circle_sub_pt - ));
DESCR("subtract");
DATA(insert OID = 1148 (  circle_mul_pt    PGUID 11 f t t 2 f 718 "718 600" 100 0 0 100  circle_mul_pt - ));
DESCR("multiply");
DATA(insert OID = 1149 (  circle_div_pt    PGUID 11 f t t 2 f 718 "718 600" 100 0 0 100  circle_div_pt - ));
DESCR("divide");
#endif
DATA(insert OID = 1150 (  timestamp_in	   PGUID 11 f t f 1 f 1184 "0" 100 0 0 100	timestamp_in - ));
DESCR("(internal)");
DATA(insert OID = 1151 (  timestamp_out    PGUID 11 f t f 1 f	23 "0" 100 0 0 100	timestamp_out - ));
DESCR("(internal)");
DATA(insert OID = 1152 (  timestamp_eq	   PGUID 11 f t f 2 f	16 "1184 1184" 100 0 0 100	timestamp_eq - ));
DESCR("equal");
DATA(insert OID = 1153 (  timestamp_ne	   PGUID 11 f t f 2 f	16 "1184 1184" 100 0 0 100	timestamp_ne - ));
DESCR("not equal");
DATA(insert OID = 1154 (  timestamp_lt	   PGUID 11 f t f 2 f	16 "1184 1184" 100 0 0 100	timestamp_lt - ));
DESCR("less-than");
DATA(insert OID = 1155 (  timestamp_le	   PGUID 11 f t f 2 f	16 "1184 1184" 100 0 0 100	timestamp_le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 1156 (  timestamp_ge	   PGUID 11 f t f 2 f	16 "1184 1184" 100 0 0 100	timestamp_ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 1157 (  timestamp_gt	   PGUID 11 f t f 2 f	16 "1184 1184" 100 0 0 100	timestamp_gt - ));
DESCR("greater-than");
DATA(insert OID = 1159 (  timezone		   PGUID 11 f t f 2 f	25 "25 1184" 100 0 0 100  timestamp_zone - ));
DESCR("time zone");

DATA(insert OID = 1160 (  interval_in	   PGUID 11 f t f 1 f 1186 "0" 100 0 0 100	interval_in - ));
DESCR("(internal)");
DATA(insert OID = 1161 (  interval_out	   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  interval_out - ));
DESCR("(internal)");
DATA(insert OID = 1162 (  interval_eq	   PGUID 11 f t f 2 f 16 "1186 1186" 100 0 0 100  interval_eq - ));
DESCR("equal");
DATA(insert OID = 1163 (  interval_ne	   PGUID 11 f t f 2 f 16 "1186 1186" 100 0 0 100  interval_ne - ));
DESCR("not equal");
DATA(insert OID = 1164 (  interval_lt	   PGUID 11 f t f 2 f 16 "1186 1186" 100 0 0 100  interval_lt - ));
DESCR("less-than");
DATA(insert OID = 1165 (  interval_le	   PGUID 11 f t f 2 f 16 "1186 1186" 100 0 0 100  interval_le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 1166 (  interval_ge	   PGUID 11 f t f 2 f 16 "1186 1186" 100 0 0 100  interval_ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 1167 (  interval_gt	   PGUID 11 f t f 2 f 16 "1186 1186" 100 0 0 100  interval_gt - ));
DESCR("greater-than");
DATA(insert OID = 1168 (  interval_um	   PGUID 11 f t f 1 f 1186 "1186" 100 0 0 100  interval_um - ));
DESCR("subtract");
DATA(insert OID = 1169 (  interval_pl	   PGUID 11 f t f 2 f 1186 "1186 1186" 100 0 0 100	interval_pl - ));
DESCR("addition");
DATA(insert OID = 1170 (  interval_mi	   PGUID 11 f t f 2 f 1186 "1186 1186" 100 0 0 100	interval_mi - ));
DESCR("subtract");
DATA(insert OID = 1171 (  date_part		   PGUID 11 f t f 2 f  701 "25 1184" 100 0 0 100  timestamp_part - ));
DESCR("extract field from timestamp");
DATA(insert OID = 1172 (  date_part		   PGUID 11 f t f 2 f  701 "25 1186" 100 0 0 100  interval_part - ));
DESCR("extract field from interval");

DATA(insert OID = 1173 (  timestamp		   PGUID 11 f t f 1 f 1184	"702" 100 0 0 100  abstime_timestamp - ));
DESCR("convert abstime to timestamp");
DATA(insert OID = 1174 (  timestamp		   PGUID 11 f t f 1 f 1184 "1082" 100 0 0 100  date_timestamp - ));
DESCR("convert date to timestamp");
DATA(insert OID = 1176 (  timestamp		   PGUID 11 f t f 2 f 1184 "1082 1083" 100 0 0 100	datetime_timestamp - ));
DESCR("convert date and time to timestamp");
DATA(insert OID = 1177 (  interval		   PGUID 11 f t f 1 f 1186	"703" 100 0 0 100  reltime_interval - ));
DESCR("convert reltime to interval");
DATA(insert OID = 1178 (  date			   PGUID 11 f t f 1 f 1082 "1184" 100 0 0 100  timestamp_date - ));
DESCR("convert timestamp to date");
DATA(insert OID = 1179 (  date			   PGUID 11 f t f 1 f 1082	"702" 100 0 0 100  abstime_date - ));
DESCR("convert abstime to date");
DATA(insert OID = 1180 (  abstime		   PGUID 11 f t f 1 f  702 "1184" 100 0 0 100  timestamp_abstime - ));
DESCR("convert timestamp to abstime");

DATA(insert OID = 1188 (  timestamp_mi		PGUID 11 f t f 2 f 1186 "1184 1184" 100 0 0 100  timestamp_mi - ));
DESCR("subtract");
DATA(insert OID = 1189 (  timestamp_pl_span PGUID 11 f t f 2 f 1184 "1184 1186" 100 0 0 100  timestamp_pl_span - ));
DESCR("plus");
DATA(insert OID = 1190 (  timestamp_mi_span PGUID 11 f t f 2 f 1184 "1184 1186" 100 0 0 100  timestamp_mi_span - ));
DESCR("minus");
DATA(insert OID = 1191 (  timestamp			PGUID 11 f t f 1 f 1184 "25" 100 0 0 100  text_timestamp - ));
DESCR("convert text to timestamp");
DATA(insert OID = 1192 (  text				PGUID 11 f t f 1 f	 25 "1184" 100 0 0 100	timestamp_text - ));
DESCR("convert timestamp to text");
DATA(insert OID = 1193 (  text				PGUID 11 f t f 1 f	 25 "1186" 100 0 0 100	interval_text - ));
DESCR("convert interval to text");
DATA(insert OID = 1194 (  reltime			PGUID 11 f t f 1 f	703 "1186" 100 0 0 100	interval_reltime - ));
DESCR("convert interval to reltime");
DATA(insert OID = 1195 (  timestamp_smaller PGUID 11 f t f 2 f 1184 "1184 1184" 100 0 0 100  timestamp_smaller - ));
DESCR("smaller of two");
DATA(insert OID = 1196 (  timestamp_larger	PGUID 11 f t f 2 f 1184 "1184 1184" 100 0 0 100  timestamp_larger - ));
DESCR("larger of two");
DATA(insert OID = 1197 (  interval_smaller	PGUID 11 f t f 2 f 1186 "1186 1186" 100 0 0 100  interval_smaller - ));
DESCR("smaller of two");
DATA(insert OID = 1198 (  interval_larger	PGUID 11 f t f 2 f 1186 "1186 1186" 100 0 0 100  interval_larger - ));
DESCR("larger of two");
DATA(insert OID = 1199 (  age				PGUID 11 f t f 2 f 1186 "1184 1184" 100 0 0 100  timestamp_age - ));
DESCR("date difference preserving months and years");

/* OIDS 1200 - 1299 */

DATA(insert OID = 1200 (  reltime		   PGUID 11 f t t 1 f  703 "23" 100 0 0 100  int4reltime - ));
DESCR("convert int4 to reltime");

DATA(insert OID = 1217 (  date_trunc	   PGUID 11 f t f 2 f 1184 "25 1184" 100 0 0 100  timestamp_trunc - ));
DESCR("truncate timestamp to specified units");
DATA(insert OID = 1218 (  date_trunc	   PGUID 11 f t f 2 f 1186 "25 1186" 100 0 0 100  interval_trunc - ));
DESCR("truncate interval to specified units");

DATA(insert OID = 1230 (  int8abs		   PGUID 11 f t t 1 f 20 "20" 100 0 0 100  int8abs - ));
DESCR("absolute value");

DATA(insert OID = 1236 (  int8larger	   PGUID 11 f t t 2 f 20 "20 20" 100 0 0 100  int8larger - ));
DESCR("larger of two");
DATA(insert OID = 1237 (  int8smaller	   PGUID 11 f t t 2 f 20 "20 20" 100 0 0 100  int8smaller - ));
DESCR("smaller of two");

DATA(insert OID = 1238 (  texticregexeq    PGUID 11 f t t 2 f 16 "25 25" 100 0 1 0	texticregexeq - ));
DESCR("matches regex., case-insensitive");
DATA(insert OID = 1239 (  texticregexne    PGUID 11 f t t 2 f 16 "25 25" 100 0 1 0	texticregexne - ));
DESCR("does not match regex., case-insensitive");
DATA(insert OID = 1240 (  nameicregexeq    PGUID 11 f t t 2 f 16 "19 25" 100 0 0 100  nameicregexeq - ));
DESCR("matches regex., case-insensitive");
DATA(insert OID = 1241 (  nameicregexne    PGUID 11 f t t 2 f 16 "19 25" 100 0 0 100  nameicregexne - ));
DESCR("does not match regex., case-insensitive");

DATA(insert OID = 1251 (  int4abs		   PGUID 11 f t t 1 f 23 "23" 100 0 0 100  int4abs - ));
DESCR("absolute value");
DATA(insert OID = 1253 (  int2abs		   PGUID 11 f t t 1 f 21 "21" 100 0 0 100  int2abs - ));
DESCR("absolute value");

DATA(insert OID = 1263 (  interval		   PGUID 11 f t f 1 f 1186 "25" 100 0 0 100  text_interval - ));
DESCR("convert text to interval");

DATA(insert OID = 1271 (  overlaps		   PGUID 11 f t t 4 f 16 "1266 1266 1266 1266" 100 0 1 0  overlaps_timetz - ));
DESCR("SQL92 interval comparison");
DATA(insert OID = 1272 (  datetime_pl	   PGUID 11 f t f 2 f 1184 "1082 1083" 100 0 0 100	datetime_timestamp - ));
DESCR("convert date and time to timestamp");

DATA(insert OID = 1274 (  int84pl		   PGUID 11 f t t 2 f 20 "20 23" 100 0 0 100  int84pl - ));
DESCR("addition");
DATA(insert OID = 1275 (  int84mi		   PGUID 11 f t t 2 f 20 "20 23" 100 0 0 100  int84mi - ));
DESCR("subtraction");
DATA(insert OID = 1276 (  int84mul		   PGUID 11 f t t 2 f 20 "20 23" 100 0 0 100  int84mul - ));
DESCR("multiply");
DATA(insert OID = 1277 (  int84div		   PGUID 11 f t t 2 f 20 "20 23" 100 0 0 100  int84div - ));
DESCR("divide");
DATA(insert OID = 1278 (  int48pl		   PGUID 11 f t t 2 f 20 "23 20" 100 0 0 100  int48pl - ));
DESCR("addition");
DATA(insert OID = 1279 (  int48mi		   PGUID 11 f t t 2 f 20 "23 20" 100 0 0 100  int48mi - ));
DESCR("subtraction");
DATA(insert OID = 1280 (  int48mul		   PGUID 11 f t t 2 f 20 "23 20" 100 0 0 100  int48mul - ));
DESCR("multiply");
DATA(insert OID = 1281 (  int48div		   PGUID 11 f t t 2 f 20 "23 20" 100 0 0 100  int48div - ));
DESCR("divide");

DATA(insert OID = 1288 (  text			   PGUID 11 f t t 1 f 25 "20" 100 0 0 100  int8_text - ));
DESCR("convert int8 to text");
DATA(insert OID = 1289 (  int8			   PGUID 11 f t t 1 f 20 "25" 100 0 0 100  text_int8 - ));
DESCR("convert text to int8");

DATA(insert OID = 1290 (  _bpchar		   PGUID 11 f t t 2 f 1014 "1014 23" 100 0 0 100  _bpchar - ));
DESCR("truncate _char()");
DATA(insert OID = 1291 (  _varchar		   PGUID 11 f t t 2 f 1015 "1015 23" 100 0 0 100  _varchar - ));
DESCR("truncate _varchar()");

DATA(insert OID = 1292 ( tideq			   PGUID 11 f t f 2 f 16 "27 27" 100 0 0 100  tideq - ));
DESCR("equal");
DATA(insert OID = 1293 ( currtid		   PGUID 11 f t f 2 f 27 "26 27" 100 0 0 100  currtid_byreloid - ));
DESCR("latest tid of a tuple");
DATA(insert OID = 1294 ( currtid2		   PGUID 11 f t f 2 f 27 "25 27" 100 0 0 100  currtid_byrelname - ));
DESCR("latest tid of a tuple");
DATA(insert OID = 1513 ( tid		   PGUID 11 f t t 1 f 27 "17" 100 0 0 100  tidtobytes - ));
DESCR("tid to bytea");
DATA(insert OID = 1514 ( bytea		   PGUID 11 f t t 1 f 17 "27" 100 0 0 100  bytestotid - ));
DESCR("bytea to tid");

DATA(insert OID = 1296 (  timedate_pl	   PGUID 14 f t f 2 f 1184 "1083 1082" 100 0 0 100	"select datetime_pl($2, $1)" - ));
DESCR("convert time and date to timestamp");
DATA(insert OID = 1297 (  datetimetz_pl    PGUID 11 f t f 2 f 1184 "1082 1266" 100 0 0 100	datetimetz_timestamp - ));
DESCR("convert date and time with time zone to timestamp");
DATA(insert OID = 1298 (  timetzdate_pl    PGUID 14 f t f 2 f 1184 "1266 1082" 100 0 0 100	"select datetimetz_pl($2, $1)" - ));
DESCR("convert time with time zone and date to timestamp");
DATA(insert OID = 1299 (  now			   PGUID 11 f t f 0 f 1184 "0" 100 0 0 100	now - ));
DESCR("current transaction time");

/* OIDS 1300 - 1399 */
#ifdef USEGEO
DATA(insert OID = 1300 (  positionsel		   PGUID 11 f t f 5 f 701 "26 26 21 0 23" 100 0 0 100  positionsel - ));
DESCR("restriction selectivity for position-comparison operators");
DATA(insert OID = 1301 (  positionjoinsel	   PGUID 11 f t f 5 f 701 "26 26 21 26 21" 100 0 0 100	positionjoinsel - ));
DESCR("join selectivity for position-comparison operators");
DATA(insert OID = 1302 (  contsel		   PGUID 11 f t f 5 f 701 "26 26 21 0 23" 100 0 0 100  contsel - ));
DESCR("restriction selectivity for containment comparison operators");
DATA(insert OID = 1303 (  contjoinsel	   PGUID 11 f t f 5 f 701 "26 26 21 26 21" 100 0 0 100	contjoinsel - ));
DESCR("join selectivity for containment comparison operators");
#endif  /* USEGEO  */
DATA(insert OID = 1304 ( overlaps			 PGUID 11 f t t 4 f 16 "1184 1184 1184 1184" 100 0 1 0	overlaps_timestamp - ));
DESCR("SQL92 interval comparison");
DATA(insert OID = 1305 ( overlaps			 PGUID 14 f t t 4 f 16 "1184 1186 1184 1186" 100 0 1 0	"select overlaps($1, ($1 + $2), $3, ($3 + $4))" - ));
DESCR("SQL92 interval comparison");
DATA(insert OID = 1306 ( overlaps			 PGUID 14 f t t 4 f 16 "1184 1184 1184 1186" 100 0 1 0	"select overlaps($1, $2, $3, ($3 + $4))" - ));
DESCR("SQL92 interval comparison");
DATA(insert OID = 1307 ( overlaps			 PGUID 14 f t t 4 f 16 "1184 1186 1184 1184" 100 0 1 0	"select overlaps($1, ($1 + $2), $3, $4)" - ));
DESCR("SQL92 interval comparison");

DATA(insert OID = 1308 ( overlaps			 PGUID 11 f t t 4 f 16 "1083 1083 1083 1083" 100 0 1 0	overlaps_time - ));
DESCR("SQL92 interval comparison");
DATA(insert OID = 1309 ( overlaps			 PGUID 14 f t t 4 f 16 "1083 1186 1083 1186" 100 0 1 0	"select overlaps($1, ($1 + $2), $3, ($3 + $4))" - ));
DESCR("SQL92 interval comparison");
DATA(insert OID = 1310 ( overlaps			 PGUID 14 f t t 4 f 16 "1083 1083 1083 1186" 100 0 1 0	"select overlaps($1, $2, $3, ($3 + $4))" - ));
DESCR("SQL92 interval comparison");
DATA(insert OID = 1311 ( overlaps			 PGUID 14 f t t 4 f 16 "1083 1186 1083 1083" 100 0 1 0	"select overlaps($1, ($1 + $2), $3, $4)" - ));
DESCR("SQL92 interval comparison");

DATA(insert OID = 1314 (  timestamp_cmp		 PGUID 11 f t f 2 f   23 "1184 1184" 100 0 0 100  timestamp_cmp - ));
DESCR("less-equal-greater");
DATA(insert OID = 1315 (  interval_cmp		 PGUID 11 f t f 2 f   23 "1186 1186" 100 0 0 100  interval_cmp - ));
DESCR("less-equal-greater");
DATA(insert OID = 1316 (  time				 PGUID 11 f t f 1 f 1083 "1184" 100 0 0 100  timestamp_time - ));
DESCR("convert timestamp to time");

DATA(insert OID = 1833 (  length			 PGUID 11 f t t 1 f   20 "1803" 100 0 1 0  bloblen - ));
DESCR("length");

DATA(insert OID = 1317 (  length			 PGUID 11 f t t 1 f   23 "25" 100 0 1 0  textlen - ));
DESCR("length");
DATA(insert OID = 1318 (  length			 PGUID 11 f t t 1 f   23 "1042" 100 0 0 100  bpcharlen - ));
DESCR("character length");
DATA(insert OID = 1319 (  length			 PGUID 11 f t t 1 f   23 "1043" 100 0 0 100  varcharlen - ));
DESCR("character length");

DATA(insert OID = 1326 (  interval_div		 PGUID 11 f t f 2 f 1186 "1186 701" 100 0 0 100  interval_div - ));
DESCR("divide");

DATA(insert OID = 1339 (  dlog10			 PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dlog10 - ));
DESCR("base 10 logarithm");
DATA(insert OID = 1340 (  log				 PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dlog10 - ));
DESCR("base 10 logarithm");
DATA(insert OID = 1341 (  ln				 PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dlog1 - ));
DESCR("natural logarithm");
DATA(insert OID = 1342 (  round				 PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dround - ));
DESCR("round to integral part");
DATA(insert OID = 1343 (  trunc				 PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dtrunc - ));
DESCR("truncate to integral part");
DATA(insert OID = 1344 (  sqrt				 PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dsqrt - ));
DESCR("square root");
DATA(insert OID = 1345 (  cbrt				 PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dcbrt - ));
DESCR("cube root");
DATA(insert OID = 1346 (  pow				 PGUID 11 f t t 2 f 701 "701 701" 100 0 0 100  dpow - ));
DESCR("exponentiation");
DATA(insert OID = 1347 (  exp				 PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dexp - ));
DESCR("exponential");

DATA(insert OID = 1348 (  obj_description	 PGUID 14 f t f 1 f   25 "26" 100 0 0 100  "select description from pg_description where objoid = $1" - ));
DESCR("get description for object id");
DATA(insert OID = 1349 (  oidvectortypes	 PGUID 11 f t f 1 f   25 "30" 100 0 0 100  oidvectortypes - ));
DESCR("print type names of oidvector field");


DATA(insert OID = 1350 (  timetz_in		   PGUID 11 f t f 1 f 1266 "0" 100 0 0 100	timetz_in - ));
DESCR("(internal)");
DATA(insert OID = 1351 (  timetz_out	   PGUID 11 f t f 1 f 23 "0" 100 0 0 100  timetz_out - ));
DESCR("(internal)");
DATA(insert OID = 1352 (  timetz_eq		   PGUID 11 f t t 2 f 16 "1266 1266" 100 0 0 100  timetz_eq - ));
DESCR("equal");
DATA(insert OID = 1353 (  timetz_ne		   PGUID 11 f t t 2 f 16 "1266 1266" 100 0 0 100  timetz_ne - ));
DESCR("not equal");
DATA(insert OID = 1354 (  timetz_lt		   PGUID 11 f t t 2 f 16 "1266 1266" 100 0 0 100  timetz_lt - ));
DESCR("less-than");
DATA(insert OID = 1355 (  timetz_le		   PGUID 11 f t t 2 f 16 "1266 1266" 100 0 0 100  timetz_le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 1356 (  timetz_ge		   PGUID 11 f t t 2 f 16 "1266 1266" 100 0 0 100  timetz_ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 1357 (  timetz_gt		   PGUID 11 f t t 2 f 16 "1266 1266" 100 0 0 100  timetz_gt - ));
DESCR("greater-than");
DATA(insert OID = 1358 (  timetz_cmp	   PGUID 11 f t t 2 f 23 "1266 1266" 100 0 0 100  timetz_cmp - ));
DESCR("less-equal-greater");
DATA(insert OID = 1359 (  timestamp		   PGUID 11 f t f 2 f 1184 "1082 1266" 100 0 0 100	datetimetz_timestamp - ));
DESCR("convert date and time with time zone to timestamp");

DATA(insert OID = 1362 (  time				 PGUID 14 f t t 1 f 1083 "1083" 100 0 0 100  "select $1" - ));
DESCR("convert (noop)");
DATA(insert OID = 1364 (  time				 PGUID 14 f t f 1 f 1083 "702" 100 0 0 100	"select time(timestamp($1))" - ));
DESCR("convert abstime to time");
DATA(insert OID = 1365 (  abstime			 PGUID 14 f t f 1 f  702 "702" 100 0 0 100	"select $1" - ));
DESCR("convert (noop)");
DATA(insert OID = 1367 (  reltime			 PGUID 14 f t t 1 f  703 "703" 100 0 0 100	"select $1" - ));
DESCR("convert (noop)");
DATA(insert OID = 1368 (  timestamp			 PGUID 14 f t f 1 f 1184 "1184" 100 0 0 100  "select $1" - ));
DESCR("convert (noop)");
DATA(insert OID = 1369 (  interval			 PGUID 14 f t t 1 f 1186 "1186" 100 0 0 100  "select $1" - ));
DESCR("convert (noop)");
DATA(insert OID = 1370 (  interval			 PGUID 11 f t f 1 f 1186 "1083" 100 0 0 100  time_interval - ));
DESCR("convert time to interval");
DATA(insert OID = 1371 (  date				 PGUID 14 f t t 1 f 1082 "1082" 100 0 0 100  "select $1" - ));
DESCR("convert (noop)");
DATA(insert OID = 1372 (  char_length		 PGUID 11 f t t 1 f   23   "1042" 100 0 0 100  bpcharlen - ));
DESCR("character length");
DATA(insert OID = 1373 (  char_length		 PGUID 11 f t t 1 f   23   "1043" 100 0 0 100  varcharlen - ));
DESCR("character length");

DATA(insert OID = 1374 (  octet_length			 PGUID 11 f t t 1 f   23   "25" 100 0 0 100  textoctetlen - ));
DESCR("octet length");
DATA(insert OID = 1375 (  octet_length			 PGUID 11 f t t 1 f   23   "1042" 100 0 0 100  bpcharoctetlen - ));
DESCR("octet length");
DATA(insert OID = 1376 (  octet_length			 PGUID 11 f t t 1 f   23   "1043" 100 0 0 100  varcharoctetlen - ));
DESCR("octet length");

DATA(insert OID = 1377 (  time_larger	   PGUID 11 f t t 2 f 1083 "1083 1083" 100 0 0 100	time_larger - ));
DESCR("larger of two");
DATA(insert OID = 1378 (  time_smaller	   PGUID 11 f t t 2 f 1083 "1083 1083" 100 0 0 100	time_smaller - ));
DESCR("smaller of two");
DATA(insert OID = 1379 (  timetz_larger    PGUID 11 f t t 2 f 1083 "1266 1266" 100 0 0 100	timetz_larger - ));
DESCR("larger of two");
DATA(insert OID = 1380 (  timetz_smaller   PGUID 11 f t t 2 f 1083 "1266 1266" 100 0 0 100	timetz_smaller - ));
DESCR("smaller of two");

DATA(insert OID = 1381 (  char_length	   PGUID 11 f t t 1 f 23 "25" 100 0 1 0  textlen - ));
DESCR("length");

DATA(insert OID = 1382 (  date_part    PGUID 14 f t f 2 f  701 "25 702" 100 0 0 100  "select date_part($1, timestamp($2))" - ));
DESCR("extract field from abstime");
DATA(insert OID = 1383 (  date_part    PGUID 14 f t f 2 f  701 "25 703" 100 0 0 100  "select date_part($1, interval($2))" - ));
DESCR("extract field from reltime");
DATA(insert OID = 1384 (  date_part    PGUID 14 f t f 2 f  701 "25 1082" 100 0 0 100  "select date_part($1, timestamp($2))" - ));
DESCR("extract field from date");
DATA(insert OID = 1385 (  date_part    PGUID 14 f t f 2 f  701 "25 1083" 100 0 0 100  "select date_part($1, interval($2))" - ));
DESCR("extract field from time");
DATA(insert OID = 1386 (  age		   PGUID 14 f t f 1 f 1186 "1184" 100 0 0 100  "select age(\'today\', $1)" - ));
DESCR("date difference from today preserving months and years");

DATA(insert OID = 1387 (  timetz		   PGUID 14 f t f 1 f 1266 "1266" 100 0 0 100  "select $1" - ));
DESCR("noop conversion");
DATA(insert OID = 1388 (  timetz		   PGUID 11 f t f 1 f 1266 "1184" 100 0 0 100  timestamp_timetz - ));
DESCR("convert timestamp to time");

DATA(insert OID = 1389 (  isfinite	   PGUID 11 f t f 1 f	16 "1184" 100 0 0 100  timestamp_finite - ));
DESCR("boolean test");
DATA(insert OID = 1390 (  isfinite	   PGUID 11 f t f 1 f	16 "1186" 100 0 0 100  interval_finite - ));
DESCR("boolean test");


DATA(insert OID = 1391 (  factorial		   PGUID 11 f t t 1 f 23 "21" 100 0 0 100  int2fac - ));
DESCR("factorial");
DATA(insert OID = 1392 (  factorial		   PGUID 11 f t t 1 f 23 "23" 100 0 0 100  int4fac - ));
DESCR("factorial");
DATA(insert OID = 1393 (  factorial		   PGUID 11 f t t 1 f 20 "20" 100 0 0 100  int8fac - ));
DESCR("factorial");
DATA(insert OID = 1394 (  abs			   PGUID 11 f t t 1 f 700 "700" 100 0 0 100  float4abs - ));
DESCR("absolute value");
DATA(insert OID = 1395 (  abs			   PGUID 11 f t t 1 f 701 "701" 100 0 0 100  float8abs - ));
DESCR("absolute value");
DATA(insert OID = 1396 (  abs			   PGUID 11 f t t 1 f 20 "20" 100 0 0 100  int8abs - ));
DESCR("absolute value");
DATA(insert OID = 1397 (  abs			   PGUID 11 f t t 1 f 23 "23" 100 0 0 100  int4abs - ));
DESCR("absolute value");
DATA(insert OID = 1398 (  abs			   PGUID 11 f t t 1 f 21 "21" 100 0 0 100  int2abs - ));
DESCR("absolute value");

/* OIDS 1400 - 1499 */

DATA(insert OID = 1400 (  name		   PGUID 11 f t t 1 f	19 "1043" 100 0 0 100  text_name - ));
DESCR("convert varchar to name");
DATA(insert OID = 1401 (  varchar	   PGUID 11 f t t 1 f 1043 "19" 100 0 0 100  name_text - ));
DESCR("convert convert name to varchar");

DATA(insert OID = 1402 (  float4	   PGUID 14 f t t 1 f  700	"700" 100 0 0 100  "select $1" - ));
DESCR("convert float4 to float4 (no-op)");
DATA(insert OID = 1403 (  int2		   PGUID 14 f t t 1 f	21	 "21" 100 0 0 100  "select $1" - ));
DESCR("convert (no-op)");
DATA(insert OID = 1404 (  float8	   PGUID 14 f t t 1 f  701	"701" 100 0 0 100  "select $1" - ));
DESCR("convert (no-op)");
DATA(insert OID = 1405 (  int4		   PGUID 14 f t t 1 f	23	 "23" 100 0 0 100  "select $1" - ));
DESCR("convert (no-op)");

#ifdef USEGEO
DATA(insert OID = 1406 (  isvertical		PGUID 11 f t t 2 f	16 "600 600" 100 0 0 100	point_vert - ));
DESCR("vertical?");
DATA(insert OID = 1407 (  ishorizontal		PGUID 11 f t t 2 f	16 "600 600" 100 0 0 100	point_horiz - ));
DESCR("horizontal?");
DATA(insert OID = 1408 (  isparallel		PGUID 11 f t t 2 f	16 "601 601" 100 0 0 100	lseg_parallel - ));
DESCR("parallel?");
DATA(insert OID = 1409 (  isperp			PGUID 11 f t t 2 f	16 "601 601" 100 0 0 100	lseg_perp - ));
DESCR("perpendicular?");
DATA(insert OID = 1410 (  isvertical		PGUID 11 f t t 1 f	16 "601" 100 0 0 100	lseg_vertical - ));
DESCR("vertical?");
DATA(insert OID = 1411 (  ishorizontal		PGUID 11 f t t 1 f	16 "601" 100 0 0 100	lseg_horizontal - ));
DESCR("horizontal?");
DATA(insert OID = 1412 (  isparallel		PGUID 11 f t t 2 f	16 "628 628" 100 0 0 100  line_parallel - ));
DESCR("lines parallel?");
DATA(insert OID = 1413 (  isperp			PGUID 11 f t t 2 f	16 "628 628" 100 0 0 100  line_perp - ));
DESCR("lines perpendicular?");
DATA(insert OID = 1414 (  isvertical		PGUID 11 f t t 1 f	16 "628" 100 0 0 100  line_vertical - ));
DESCR("lines vertical?");
DATA(insert OID = 1415 (  ishorizontal		PGUID 11 f t t 1 f	16 "628" 100 0 0 100  line_horizontal - ));
DESCR("lines horizontal?");
DATA(insert OID = 1416 (  point				PGUID 11 f t t 1 f 600 "718" 100 0 1 0	circle_center - ));
DESCR("center of");

DATA(insert OID = 1421 (  rect				PGUID 11 f t t 2 f 603 "600 600" 100 0 0 100  rect - ));
DESCR("convert points to box");
DATA(insert OID = 1422 (  rect_add			PGUID 11 f t t 2 f 603 "603 600" 100 0 0 100  rect_add - ));
DESCR("add point to box (translate)");
DATA(insert OID = 1423 (  rect_sub			PGUID 11 f t t 2 f 603 "603 600" 100 0 0 100  rect_sub - ));
DESCR("subtract point from box (translate)");
DATA(insert OID = 1424 (  rect_mul			PGUID 11 f t t 2 f 603 "603 600" 100 0 0 100  rect_mul - ));
DESCR("multiply box by point (scale)");
DATA(insert OID = 1425 (  rect_div			PGUID 11 f t t 2 f 603 "603 600" 100 0 0 100  rect_div - ));
DESCR("divide box by point (scale)");
DATA(insert OID = 1426 (  path_contain_pt	PGUID 14 f t t 2 f	16 "602 600" 100 0 0 100  "select on_ppath($2, $1)" - ));
DESCR("path contains point?");
DATA(insert OID = 1428 (  poly_contain_pt	PGUID 11 f t t 2 f	16 "604 600" 100 0 0 100  poly_contain_pt - ));
DESCR("polygon contains point?");
DATA(insert OID = 1429 (  pt_contained_poly PGUID 11 f t t 2 f	16 "600 604" 100 0 0 100  pt_contained_poly - ));
DESCR("point contained by polygon?");

DATA(insert OID = 1430 (  isclosed			PGUID 11 f t t 1 f	16 "602" 100 0 0 100  path_isclosed - ));
DESCR("path closed?");
DATA(insert OID = 1431 (  isopen			PGUID 11 f t t 1 f	16 "602" 100 0 0 100  path_isopen - ));
DESCR("path open?");
DATA(insert OID = 1432 (  path_npoints		PGUID 11 f t t 1 f	23 "602" 100 0 0 100  path_npoints - ));
DESCR("# points in path");

/* pclose and popen might better be named close and open, but that crashes initdb.
 * - thomas 97/04/20
 */
DATA(insert OID = 1433 (  pclose			PGUID 11 f t t 1 f 602 "602" 100 0 0 100  path_close - ));
DESCR("close path");
DATA(insert OID = 1434 (  popen				PGUID 11 f t t 1 f 602 "602" 100 0 0 100  path_open - ));
DESCR("open path");
DATA(insert OID = 1435 (  path_add			PGUID 11 f t t 2 f 602 "602 602" 100 0 0 100  path_add - ));
DESCR("addition");
DATA(insert OID = 1436 (  path_add_pt		PGUID 11 f t t 2 f 602 "602 600" 100 0 0 100  path_add_pt - ));
DESCR("addition");
DATA(insert OID = 1437 (  path_sub_pt		PGUID 11 f t t 2 f 602 "602 600" 100 0 0 100  path_sub_pt - ));
DESCR("subtract");
DATA(insert OID = 1438 (  path_mul_pt		PGUID 11 f t t 2 f 602 "602 600" 100 0 0 100  path_mul_pt - ));
DESCR("multiply");
DATA(insert OID = 1439 (  path_div_pt		PGUID 11 f t t 2 f 602 "602 600" 100 0 0 100  path_div_pt - ));
DESCR("divide");

DATA(insert OID = 1440 (  point				PGUID 11 f t t 2 f 600 "701 701" 100 0 0 100  point - ));
DESCR("convert x, y to point");
DATA(insert OID = 1441 (  point_add			PGUID 11 f t t 2 f 600 "600 600" 100 0 0 100  point_add - ));
DESCR("add points (translate)");
DATA(insert OID = 1442 (  point_sub			PGUID 11 f t t 2 f 600 "600 600" 100 0 0 100  point_sub - ));
DESCR("subtract points (translate)");
DATA(insert OID = 1443 (  point_mul			PGUID 11 f t t 2 f 600 "600 600" 100 0 0 100  point_mul - ));
DESCR("multiply points (scale/rotate)");
DATA(insert OID = 1444 (  point_div			PGUID 11 f t t 2 f 600 "600 600" 100 0 0 100  point_div - ));
DESCR("divide points (scale/rotate)");

DATA(insert OID = 1445 (  poly_npoints		PGUID 11 f t t 1 f	23 "604" 100 0 0 100  poly_npoints - ));
DESCR("number of points in polygon");
DATA(insert OID = 1446 (  rect				PGUID 11 f t t 1 f 603 "604" 100 0 0 100  poly_box - ));
DESCR("convert polygon to bounding box");
DATA(insert OID = 1447 (  path				PGUID 11 f t t 1 f 602 "604" 100 0 0 100  poly_path - ));
DESCR("convert polygon to path");
DATA(insert OID = 1448 (  polygon			PGUID 11 f t t 1 f 604 "603" 100 0 0 100  rect_poly - ));
DESCR("convert box to polygon");
DATA(insert OID = 1449 (  polygon			PGUID 11 f t t 1 f 604 "602" 100 0 0 100  path_poly - ));
DESCR("convert path to polygon");

DATA(insert OID = 1450 (  circle_in			PGUID 11 f t t 1 f 718 "0" 100 0 1 0  circle_in - ));
DESCR("(internal)");
DATA(insert OID = 1451 (  circle_out		PGUID 11 f t t 1 f	23	"0" 100 0 1 0  circle_out - ));
DESCR("(internal)");
DATA(insert OID = 1452 (  circle_same		PGUID 11 f t t 2 f	16 "718 718" 100 0 1 0	circle_same - ));
DESCR("same as");
DATA(insert OID = 1453 (  circle_contain	PGUID 11 f t t 2 f	16 "718 718" 100 0 1 0	circle_contain - ));
DESCR("contains");
DATA(insert OID = 1454 (  circle_left		PGUID 11 f t t 2 f	16 "718 718" 100 0 1 0	circle_left - ));
DESCR("is left of");
DATA(insert OID = 1455 (  circle_overleft	PGUID 11 f t t 2 f	16 "718 718" 100 0 1 0	circle_overleft - ));
DESCR("overlaps, but does not extend to right of");
DATA(insert OID = 1456 (  circle_overright	PGUID 11 f t t 2 f	16 "718 718" 100 0 1 0	circle_overright - ));
DESCR("");
DATA(insert OID = 1457 (  circle_right		PGUID 11 f t t 2 f	16 "718 718" 100 0 1 0	circle_right - ));
DESCR("is left of");
DATA(insert OID = 1458 (  circle_contained	PGUID 11 f t t 2 f	16 "718 718" 100 0 1 0	circle_contained - ));
DESCR("");
DATA(insert OID = 1459 (  circle_overlap	PGUID 11 f t t 2 f	16 "718 718" 100 0 1 0	circle_overlap - ));
DESCR("overlaps");
DATA(insert OID = 1460 (  circle_below		PGUID 11 f t t 2 f	16 "718 718" 100 0 1 0	circle_below - ));
DESCR("is below");
DATA(insert OID = 1461 (  circle_above		PGUID 11 f t t 2 f	16 "718 718" 100 0 1 0	circle_above - ));
DESCR("is above");
DATA(insert OID = 1462 (  circle_eq			PGUID 11 f t t 2 f	16 "718 718" 100 0 1 0	circle_eq - ));
DESCR("equal");
DATA(insert OID = 1463 (  circle_ne			PGUID 11 f t t 2 f	16 "718 718" 100 0 1 0	circle_ne - ));
DESCR("not equal");
DATA(insert OID = 1464 (  circle_lt			PGUID 11 f t t 2 f	16 "718 718" 100 0 1 0	circle_lt - ));
DESCR("less-than");
DATA(insert OID = 1465 (  circle_gt			PGUID 11 f t t 2 f	16 "718 718" 100 0 1 0	circle_gt - ));
DESCR("greater-than");
DATA(insert OID = 1466 (  circle_le			PGUID 11 f t t 2 f	16 "718 718" 100 0 1 0	circle_le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 1467 (  circle_ge			PGUID 11 f t t 2 f	16 "718 718" 100 0 1 0	circle_ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 1468 (  area				PGUID 11 f t t 1 f 701 "718" 100 0 1 0	circle_area - ));
DESCR("area of circle");
DATA(insert OID = 1469 (  diameter			PGUID 11 f t t 1 f 701 "718" 100 0 1 0	circle_diameter - ));
DESCR("diameter of circle");
DATA(insert OID = 1470 (  radius			PGUID 11 f t t 1 f 701 "718" 100 0 1 0	circle_radius - ));
DESCR("radius of circle");
DATA(insert OID = 1471 (  circle_distance	PGUID 11 f t t 2 f 701 "718 718" 100 0 1 0	circle_distance - ));
DESCR("distance between");
DATA(insert OID = 1472 (  circle_center		PGUID 11 f t t 1 f 600 "718" 100 0 1 0	circle_center - ));
DESCR("center of");
DATA(insert OID = 1473 (  circle			PGUID 11 f t t 2 f 718 "600 701" 100 0 1 0	circle - ));
DESCR("convert point and radius to circle");
DATA(insert OID = 1474 (  circle			PGUID 11 f t t 1 f 718 "604" 100 0 1 0	poly_circle - ));
DESCR("convert polygon to circle");
DATA(insert OID = 1475 (  polygon			PGUID 11 f t t 2 f 604 "23 718" 100 0 1 0  circle_poly - ));
DESCR("convert vertex count and circle to polygon");
DATA(insert OID = 1476 (  dist_pc			PGUID 11 f t t 2 f 701 "600 718" 100 0 1 0	dist_pc - ));
DESCR("distance between point and circle");
DATA(insert OID = 1477 (  circle_contain_pt PGUID 11 f t t 2 f	16 "718 600" 100 0 0 100  circle_contain_pt - ));
DESCR("circle contains point?");
DATA(insert OID = 1478 (  pt_contained_circle	PGUID 11 f t t 2 f	16 "600 718" 100 0 0 100  pt_contained_circle - ));
DESCR("point inside circle?");
DATA(insert OID = 1479 (  circle			PGUID 11 f t t 1 f 718 "603" 100 0 1 0	rect_circle - ));
DESCR("convert box to circle");
DATA(insert OID = 1480 (  rect				PGUID 11 f t t 1 f 603 "718" 100 0 1 0	circle_box - ));
DESCR("convert circle to box");
DATA(insert OID = 1481 (  tinterval			 PGUID 11 f t f 2 f 704 "702 702" 100 0 0 100 mktinterval - ));
DESCR("convert to tinterval");

DATA(insert OID = 1482 (  lseg_ne			PGUID 11 f t t 2 f	16 "601 601" 100 0 0 100  lseg_ne - ));
DESCR("not equal");
DATA(insert OID = 1483 (  lseg_lt			PGUID 11 f t t 2 f	16 "601 601" 100 0 0 100  lseg_lt - ));
DESCR("less-than");
DATA(insert OID = 1484 (  lseg_le			PGUID 11 f t t 2 f	16 "601 601" 100 0 0 100  lseg_le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 1485 (  lseg_gt			PGUID 11 f t t 2 f	16 "601 601" 100 0 0 100  lseg_gt - ));
DESCR("greater-than");
DATA(insert OID = 1486 (  lseg_ge			PGUID 11 f t t 2 f	16 "601 601" 100 0 0 100  lseg_ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 1487 (  lseg_length		PGUID 11 f t t 1 f 701 "601" 100 0 1 0	lseg_length - ));
DESCR("distance between endpoints");
DATA(insert OID = 1488 (  close_ls			PGUID 11 f t t 2 f 600 "628 601" 100 0 10 100  close_ls - ));
DESCR("closest point to line on line segment");
DATA(insert OID = 1489 (  close_lseg		PGUID 11 f t t 2 f 600 "601 601" 100 0 10 100  close_lseg - ));
DESCR("closest point to line segment on line segment");

DATA(insert OID = 1490 (  line_in			PGUID 11 f t t 1 f 628 "0" 100 0 0 100	line_in - ));
DESCR("(internal)");
DATA(insert OID = 1491 (  line_out			PGUID 11 f t t 1 f 23  "0" 100 0 0 100	line_out - ));
DESCR("(internal)");
DATA(insert OID = 1492 (  line_eq			PGUID 11 f t t 2 f	16 "628 628" 100 0 0 100  line_eq - ));
DESCR("lines equal?");
DATA(insert OID = 1493 (  line				PGUID 11 f t t 2 f 628 "600 600" 100 0 0 100  line_construct_pp - ));
DESCR("line from points");
DATA(insert OID = 1494 (  line_interpt		PGUID 11 f t t 2 f 600 "628 628" 100 0 0 100  line_interpt - ));
DESCR("intersection point");
DATA(insert OID = 1495 (  line_intersect	PGUID 11 f t t 2 f	16 "628 628" 100 0 0 100  line_intersect - ));
DESCR("lines intersect?");
DATA(insert OID = 1496 (  line_parallel		PGUID 11 f t t 2 f	16 "628 628" 100 0 0 100  line_parallel - ));
DESCR("lines parallel?");
DATA(insert OID = 1497 (  line_perp			PGUID 11 f t t 2 f	16 "628 628" 100 0 0 100  line_perp - ));
DESCR("lines perpendicular?");
DATA(insert OID = 1498 (  line_vertical		PGUID 11 f t t 1 f	16 "628" 100 0 0 100  line_vertical - ));
DESCR("lines vertical?");
DATA(insert OID = 1499 (  line_horizontal	PGUID 11 f t t 1 f	16 "628" 100 0 0 100  line_horizontal - ));
DESCR("lines horizontal?");

/* OIDS 1500 - 1599 */

DATA(insert OID = 1530 (  length			PGUID 11 f t t 1 f 701 "601" 100 0 1 0	lseg_length - ));
DESCR("distance between endpoints");
DATA(insert OID = 1531 (  length			PGUID 11 f t t 1 f 701 "602" 100 0 1 0	path_length - ));
DESCR("sum of path segments");


DATA(insert OID = 1532 (  point				PGUID 11 f t t 1 f 600 "601" 100 0 0 100  lseg_center - ));
DESCR("center of");
DATA(insert OID = 1533 (  point				PGUID 11 f t t 1 f 600 "602" 100 0 0 100  path_center - ));
DESCR("center of");
DATA(insert OID = 1534 (  point				PGUID 11 f t t 1 f 600 "603" 100 1 0 100  rect_center - ));
DESCR("center of");
DATA(insert OID = 1540 (  point				PGUID 11 f t t 1 f 600 "604" 100 0 0 100  poly_center - ));
DESCR("center of");
DATA(insert OID = 1541 (  lseg				PGUID 11 f t t 1 f 601 "603" 100 0 0 100  rect_diagonal - ));
DESCR("");
DATA(insert OID = 1542 (  center			PGUID 11 f t t 1 f 600 "603" 100 1 0 100  rect_center - ));
DESCR("center of");
DATA(insert OID = 1543 (  center			PGUID 11 f t t 1 f 600 "718" 100 0 1 0	circle_center - ));
DESCR("center of");
DATA(insert OID = 1544 (  polygon			PGUID 14 f t t 1 f 604 "718" 100 0 0 100  "select polygon(12, $1)" - ));
DESCR("convert circle to 12-vertex polygon");
DATA(insert OID = 1545 (  npoints			PGUID 11 f t t 1 f	23 "602" 100 0 0 100  path_npoints - ));
DESCR("# points in path");
DATA(insert OID = 1556 (  npoints			PGUID 11 f t t 1 f	23 "604" 100 0 0 100  poly_npoints - ));
DESCR("number of points in polygon");
#endif  /* USEGEO  */

DATA(insert OID = 1564 (  zpbit_in			PGUID 11 f t t 1 f 1560 "0" 100 0 0 100  zpbit_in - ));
DESCR("(internal)");
DATA(insert OID = 1565 (  zpbit_out			PGUID 11 f t t 1 f	 23 "0" 100 0 0 100  zpbit_out - ));
DESCR("(internal)");

DATA(insert OID = 1569 (  like				PGUID 11 f t t 2 f 16 "25 25" 100 0 1 0  textlike - ));
DESCR("matches LIKE expression");
DATA(insert OID = 1570 (  notlike			PGUID 11 f t t 2 f 16 "25 25" 100 0 1 0  textnlike - ));
DESCR("does not match LIKE expression");
DATA(insert OID = 1571 (  like				PGUID 11 f t t 2 f 16 "19 25" 100 0 0 100  namelike - ));
DESCR("matches LIKE expression");
DATA(insert OID = 1572 (  notlike			PGUID 11 f t t 2 f 16 "19 25" 100 0 0 100  namenlike - ));
DESCR("does not match LIKE expression");
DATA(insert OID = 1573 (  int8				PGUID 14 f t t 1 f	20 "20" 100 0 0 100  "select $1" - ));
DESCR("convert int8 to int8 (no-op)");


/* SEQUENCEs nextval & currval functions */
DATA(insert OID = 1574 (  nextval			PGUID 11 f t f 1 f 23 "25" 100 0 0 100	nextval - ));
DESCR("sequence next value");
DATA(insert OID = 1575 (  currval			PGUID 11 f t f 1 f 23 "25" 100 0 0 100	currval - ));
DESCR("sequence current value");
DATA(insert OID = 1576 (  setval			PGUID 11 f t f 2 f 23 "25 23" 100 0 0 100  setval - ));
DESCR("sequence set value");

DATA(insert OID = 1579 (  varbit_in			PGUID 11 f t t 1 f 1562 "0" 100 0 0 100  varbit_in - ));
DESCR("(internal)");
DATA(insert OID = 1580 (  varbit_out		PGUID 11 f t t 1 f	 23 "0" 100 0 0 100  varbit_out - ));
DESCR("(internal)");

DATA(insert OID = 1581 (  biteq				PGUID 11 f t t 2 f 16 "1560 1560" 100 0 1 0  biteq - ));
DESCR("equal");
DATA(insert OID = 1582 (  bitne				PGUID 11 f t t 2 f 16 "1560 1560" 100 0 1 0  bitne - ));
DESCR("not equal");
DATA(insert OID = 1592 (  bitge				PGUID 11 f t t 2 f 16 "1560 1560" 100 0 1 0  bitge - ));
DESCR("greater than or equal");
DATA(insert OID = 1593 (  bitgt				PGUID 11 f t t 2 f 16 "1560 1560" 100 0 1 0  bitgt - ));
DESCR("greater than");
DATA(insert OID = 1594 (  bitle				PGUID 11 f t t 2 f 16 "1560 1560" 100 0 1 0  bitle - ));
DESCR("less than or equal");
DATA(insert OID = 1595 (  bitlt				PGUID 11 f t t 2 f 16 "1560 1560" 100 0 1 0  bitlt - ));
DESCR("less than");
DATA(insert OID = 1596 (  bitcmp			PGUID 11 f t t 2 f 23 "1560 1560" 100 0 1 0  bitcmp - ));
DESCR("compare");

DATA(insert OID = 1598 (  random			PGUID 11 f t f 0 f 701 "0" 100 0 0 100	drandom - ));
DESCR("radians to degrees");
DATA(insert OID = 1599 (  setseed			PGUID 11 f t t 1 f	23 "701" 100 0 0 100  setseed - ));
DESCR("radians to degrees");

/* OIDS 1600 - 1699 */

DATA(insert OID = 1600 (  asin				PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dasin - ));
DESCR("arcsine");
DATA(insert OID = 1601 (  acos				PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dacos - ));
DESCR("arcsine");
DATA(insert OID = 1602 (  atan				PGUID 11 f t t 1 f 701 "701" 100 0 0 100  datan - ));
DESCR("arctangent");
DATA(insert OID = 1603 (  atan2				PGUID 11 f t t 2 f 701 "701 701" 100 0 0 100  datan2 - ));
DESCR("arctangent, two arguments");
DATA(insert OID = 1604 (  sin				PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dsin - ));
DESCR("sine");
DATA(insert OID = 1605 (  cos				PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dcos - ));
DESCR("cosine");
DATA(insert OID = 1606 (  tan				PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dtan - ));
DESCR("tangent");
DATA(insert OID = 1607 (  cot				PGUID 11 f t t 1 f 701 "701" 100 0 0 100  dcot - ));
DESCR("cotangent");
DATA(insert OID = 1608 (  degrees			PGUID 11 f t t 1 f 701 "701" 100 0 0 100  degrees - ));
DESCR("radians to degrees");
DATA(insert OID = 1609 (  radians			PGUID 11 f t t 1 f 701 "701" 100 0 0 100  radians - ));
DESCR("radians to degrees");
DATA(insert OID = 1610 (  pi				PGUID 11 f t t 0 f 701 "0" 100 0 0 100	dpi - ));
DESCR("PI");

DATA(insert OID = 1618 (  interval_mul		PGUID 11 f t t 2 f 1186 "1186 701" 100 0 0 100	interval_mul - ));
DESCR("multiply interval");
DATA(insert OID = 1619 (  varchar			PGUID 11 f t t 1 f 1043 "23" 100 0 0 100  int4_text - ));
DESCR("convert int4 to varchar");

#ifdef ORACLE_COMPAT
DATA(insert OID = 1620 (  ascii				PGUID 11 f t t 1 f 23 "25" 100 0 0 100	ascii - ));
DESCR("convert first char to int4");
DATA(insert OID = 1621 (  ichar				PGUID 11 f t t 1 f 25 "23" 100 0 0 100	ichar - ));
DESCR("convert int4 to char");
DATA(insert OID = 1622 (  repeat			PGUID 11 f t t 2 f 25 "25 23" 100 0 0 100  repeat - ));
DESCR("replicate string int4 times");
#endif

DATA(insert OID = 1623 (  varchar			PGUID 11 f t t 1 f 1043 "20" 100 0 0 100  int8_text - ));
DESCR("convert int8 to varchar");
DATA(insert OID = 1624 (  mul_d_interval	PGUID 11 f t t 2 f 1186 "701 1186" 100 0 0 100	mul_d_interval - ));

/* OID's 1625 - 1639 LZTEXT data type */
DATA(insert OID = 1626 ( lztextin			  PGUID 11 f t t 1 f 1625 "0" 100 0 0 100  lztextin - ));
DESCR("(internal)");
DATA(insert OID = 1627 ( lztextout			  PGUID 11 f t t 1 f 23 "0" 100 0 0 100  lztextout - ));
DESCR("(internal)");
DATA(insert OID = 1629 ( text				  PGUID 11 f t t 1 f 25 "1625" 100 0 0 100	lztext_text -));
DESCR("convert lztext to text");
DATA(insert OID = 1631 ( lztext				  PGUID 11 f t t 1 f 1625 "25" 100 0 0 100	text_lztext -));
DESCR("convert text to lztext");
DATA(insert OID = 1632 ( lztext				  PGUID 14 f t t 1 f 1625 "1625" 100 0 0 100  "select $1" -));
DESCR("convert text to lztext");
DATA(insert OID = 1633 ( char_length		  PGUID 11 f t t 1 f 23 "1625" 100 0 1 0  lztextlen - ));
DESCR("length");
DATA(insert OID = 1634 ( length				  PGUID 11 f t t 1 f 23 "1625" 100 0 1 0  lztextlen - ));
DESCR("length");
DATA(insert OID = 1635 ( octet_length		  PGUID 11 f t t 1 f 23 "1625" 100 0 1 0  lztextoctetlen - ));
DESCR("octet length");
DATA(insert OID = 1636 ( lztext_cmp			  PGUID 11 f t t 2 f 23 "1625 1625" 100 0 1 0  lztext_cmp - ));
DESCR("compare lztext");
DATA(insert OID = 1637 ( lztext_eq			  PGUID 11 f t t 2 f 16 "1625 1625" 100 0 1 0  lztext_eq - ));
DESCR("equal");
DATA(insert OID = 1638 ( lztext_ne			  PGUID 11 f t t 2 f 16 "1625 1625" 100 0 1 0  lztext_ne - ));
DESCR("not equal");
DATA(insert OID = 1639 ( lztext_gt			  PGUID 11 f t t 2 f 16 "1625 1625" 100 0 1 0  lztext_gt - ));
DESCR("greater-than");
DATA(insert OID = 1664 ( lztext_ge			  PGUID 11 f t t 2 f 16 "1625 1625" 100 0 1 0  lztext_ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 1665 ( lztext_lt			  PGUID 11 f t t 2 f 16 "1625 1625" 100 0 1 0  lztext_lt - ));
DESCR("less-than");
DATA(insert OID = 1656 ( lztext_le			  PGUID 11 f t t 2 f 16 "1625 1625" 100 0 1 0  lztext_le - ));
DESCR("less-than-or-equal");

/* Oracle Compatibility Related Functions - By Edmund Mergl <E.Mergl@bawue.de> */
#ifdef ORACLE_COMPAT
DATA(insert OID =  868 (  strpos	   PGUID 11 f t t 2 f 23 "25 25" 100 0 0 100  textpos - ));
DESCR("find position of substring");
DATA(insert OID =  870 (  lower		   PGUID 11 f t t 1 f 25 "25" 100 0 0 100  lower - ));
DESCR("lowercase");
DATA(insert OID =  871 (  upper		   PGUID 11 f t t 1 f 25 "25" 100 0 0 100  upper - ));
DESCR("uppercase");
DATA(insert OID =  872 (  initcap	   PGUID 11 f t t 1 f 25 "25" 100 0 0 100  initcap - ));
DESCR("capitalize each word");
DATA(insert OID =  873 (  lpad		   PGUID 11 f t t 3 f 25 "25 23 25" 100 0 0 100  lpad - ));
DESCR("left-pad string to length");
DATA(insert OID =  874 (  rpad		   PGUID 11 f t t 3 f 25 "25 23 25" 100 0 0 100  rpad - ));
DESCR("right-pad string to length");
DATA(insert OID =  875 (  ltrim		   PGUID 11 f t t 2 f 25 "25 25" 100 0 0 100  ltrim - ));
DESCR("left-pad string to length");
DATA(insert OID =  876 (  rtrim		   PGUID 11 f t t 2 f 25 "25 25" 100 0 0 100  rtrim - ));
DESCR("right-pad string to length");
DATA(insert OID =  877 (  substr	   PGUID 11 f t t 3 f 25 "25 23 23" 100 0 0 100  text_substr - ));
DESCR("return portion of string");
DATA(insert OID =  878 (  translate    PGUID 11 f t t 3 f 25 "25 25 25" 100 0 0 100  translate - ));
DESCR("modify string by substring replacement");
DATA(insert OID =  879 (  lpad		   PGUID 14 f t t 2 f 25 "25 23" 100 0 0 100  "select lpad($1, $2, \' \')" - ));
DESCR("left-pad string to length");
DATA(insert OID =  880 (  rpad		   PGUID 14 f t t 2 f 25 "25 23" 100 0 0 100  "select rpad($1, $2, \' \')" - ));
DESCR("right-pad string to length");
DATA(insert OID =  881 (  ltrim		   PGUID 14 f t t 1 f 25 "25" 100 0 0 100  "select ltrim($1, \' \')" - ));
DESCR("remove initial characters from string");
DATA(insert OID =  882 (  rtrim		   PGUID 14 f t t 1 f 25 "25" 100 0 0 100  "select rtrim($1, \' \')" - ));
DESCR("remove trailing characters from string");
DATA(insert OID =  883 (  substr	   PGUID 14 f t t 2 f 25 "25 23" 100 0 0 100  "select substr($1, $2, -1)" - ));
DESCR("return portion of string");
DATA(insert OID =  884 (  btrim		   PGUID 11 f t t 2 f 25 "25 25" 100 0 0 100  btrim - ));
DESCR("trim both ends of string");
DATA(insert OID =  885 (  btrim		   PGUID 14 f t t 1 f 25 "25" 100 0 0 100  "select btrim($1, \' \')" - ));
DESCR("trim both ends of string");
#endif
/* for multi-byte support */
DATA(insert OID = 1039 (  getdatabaseencoding	   PGUID 11 f t f 0 f 19 "0" 100 0 0 100  getdatabaseencoding - ));
DESCR("encoding name of current database");

DATA(insert OID = 1295 (  pg_char_to_encoding	   PGUID 11 f t f 1 f 23 "19" 100 0 0 100  pg_char_to_encoding - ));
DESCR("convert encoding name to encoding id");

DATA(insert OID = 1597 (  pg_encoding_to_char	   PGUID 11 f t f 1 f 19 "23" 100 0 0 100  pg_encoding_to_char - ));
DESCR("convert encoding id to encoding name");

/* System-view support functions */
DATA(insert OID = 1640 (  pg_get_ruledef	   PGUID 11 f t f 1 f 25 "19" 100 0 0 100  pg_get_ruledef - ));
DESCR("source text of a rule");
DATA(insert OID = 1641 (  pg_get_viewdef	   PGUID 11 f t f 1 f 25 "19" 100 0 0 100  pg_get_viewdef - ));
DESCR("select statement of a view");
DATA(insert OID = 1642 (  pg_get_userbyid	   PGUID 11 f t f 1 f 19 "23" 100 0 0 100  pg_get_userbyid - ));
DESCR("user name by UID (with fallback)");
DATA(insert OID = 1643 (  pg_get_indexdef	   PGUID 11 f t f 1 f 25 "26" 100 0 0 100  pg_get_indexdef - ));
DESCR("index description");

/* Generic referential integrity constraint triggers */
DATA(insert OID = 1644 (  RI_FKey_check_ins		PGUID 11 f t f 0 f 0 "" 100 0 0 100  RI_FKey_check_ins - ));
DESCR("referential integrity FOREIGN KEY ... REFERENCES");
DATA(insert OID = 1645 (  RI_FKey_check_upd		PGUID 11 f t f 0 f 0 "" 100 0 0 100  RI_FKey_check_upd - ));
DESCR("referential integrity FOREIGN KEY ... REFERENCES");
DATA(insert OID = 1646 (  RI_FKey_cascade_del	PGUID 11 f t f 0 f 0 "" 100 0 0 100  RI_FKey_cascade_del - ));
DESCR("referential integrity ON DELETE CASCADE");
DATA(insert OID = 1647 (  RI_FKey_cascade_upd	PGUID 11 f t f 0 f 0 "" 100 0 0 100  RI_FKey_cascade_upd - ));
DESCR("referential integrity ON UPDATE CASCADE");
DATA(insert OID = 1648 (  RI_FKey_restrict_del	PGUID 11 f t f 0 f 0 "" 100 0 0 100  RI_FKey_restrict_del - ));
DESCR("referential integrity ON DELETE RESTRICT");
DATA(insert OID = 1649 (  RI_FKey_restrict_upd	PGUID 11 f t f 0 f 0 "" 100 0 0 100  RI_FKey_restrict_upd - ));
DESCR("referential integrity ON UPDATE RESTRICT");
DATA(insert OID = 1650 (  RI_FKey_setnull_del	PGUID 11 f t f 0 f 0 "" 100 0 0 100  RI_FKey_setnull_del - ));
DESCR("referential integrity ON DELETE SET NULL");
DATA(insert OID = 1651 (  RI_FKey_setnull_upd	PGUID 11 f t f 0 f 0 "" 100 0 0 100  RI_FKey_setnull_upd - ));
DESCR("referential integrity ON UPDATE SET NULL");
DATA(insert OID = 1652 (  RI_FKey_setdefault_del PGUID 11 f t f 0 f 0 "" 100 0 0 100  RI_FKey_setdefault_del - ));
DESCR("referential integrity ON DELETE SET DEFAULT");
DATA(insert OID = 1653 (  RI_FKey_setdefault_upd PGUID 11 f t f 0 f 0 "" 100 0 0 100  RI_FKey_setdefault_upd - ));
DESCR("referential integrity ON UPDATE SET DEFAULT");
DATA(insert OID = 1654 (  RI_FKey_noaction_del PGUID 11 f t f 0 f 0 "" 100 0 0 100	RI_FKey_noaction_del - ));
DESCR("referential integrity ON DELETE NO ACTION");
DATA(insert OID = 1655 (  RI_FKey_noaction_upd PGUID 11 f t f 0 f 0 "" 100 0 0 100	RI_FKey_noaction_upd - ));
DESCR("referential integrity ON UPDATE NO ACTION");

DATA(insert OID = 1666 (  varbiteq			PGUID 11 f t t 2 f 16 "1562 1562" 100 0 1 0  varbiteq - ));
DESCR("equal");
DATA(insert OID = 1667 (  varbitne			PGUID 11 f t t 2 f 16 "1562 1562" 100 0 1 0  varbitne - ));
DESCR("not equal");
DATA(insert OID = 1668 (  varbitge			PGUID 11 f t t 2 f 16 "1562 1562" 100 0 1 0  varbitge - ));
DESCR("greater than or equal");
DATA(insert OID = 1669 (  varbitgt			PGUID 11 f t t 2 f 16 "1562 1562" 100 0 1 0  varbitgt - ));
DESCR("greater than");
DATA(insert OID = 1670 (  varbitle			PGUID 11 f t t 2 f 16 "1562 1562" 100 0 1 0  varbitle - ));
DESCR("less than or equal");
DATA(insert OID = 1671 (  varbitlt			PGUID 11 f t t 2 f 16 "1562 1562" 100 0 1 0  varbitlt - ));
DESCR("less than");
DATA(insert OID = 1672 (  varbitcmp			PGUID 11 f t t 2 f 23 "1562 1562" 100 0 1 0  varbitcmp - ));
DESCR("compare");

DATA(insert OID = 1673 (  bitAnd			PGUID 11 f t t 2 f 1560 "1560 1560" 100 0 1 0  bitAnd - ));
DESCR("bitwise and");
DATA(insert OID = 1674 (  bitOr				PGUID 11 f t t 2 f 1560 "1560 1560" 100 0 1 0  bitOr - ));
DESCR("bitwise or");
DATA(insert OID = 1675 (  bitxor			PGUID 11 f t t 2 f 1560 "1560 1560" 100 0 1 0  bitxor - ));
DESCR("bitwise exclusive or");
DATA(insert OID = 1676 (  bitnot			PGUID 11 f t t 2 f 1560 "1560 1560" 100 0 1 0  bitnot - ));
DESCR("bitwise negation");
DATA(insert OID = 1677 (  bitshiftright		PGUID 11 f t t 2 f 1560 "1560 1560" 100 0 1 0  bitshiftright - ));
DESCR("bitwise right shift");
DATA(insert OID = 1678 (  bitshiftleft		PGUID 11 f t t 2 f 1560 "1560 1560" 100 0 1 0  bitshiftleft - ));
DESCR("bitwise left shift");
DATA(insert OID = 1679 (  bitcat			PGUID 11 f t t 2 f 1560 "1560 1560" 100 0 1 0  bitcat - ));
DESCR("bitwise concatenation");
DATA(insert OID = 1680 (  bitsubstr			PGUID 11 f t t 2 f 1560 "1560 1560" 100 0 1 0  bitsubstr - ));
DESCR("bitwise field");

DATA(insert OID = 1681 (  varbitand			PGUID 11 f t t 2 f 1562 "1562 1562" 100 0 1 0  varbitand - ));
DESCR("bitwise and");
DATA(insert OID = 1682 (  varbitor			PGUID 11 f t t 2 f 1562 "1562 1562" 100 0 1 0  varbitor - ));
DESCR("bitwise or");
DATA(insert OID = 1683 (  varbitxor			PGUID 11 f t t 2 f 1562 "1562 1562" 100 0 1 0  varbitxor - ));
DESCR("bitwise exclusive or");
DATA(insert OID = 1684 (  varbitnot			PGUID 11 f t t 2 f 1562 "1562 1562" 100 0 1 0  varbitnot - ));
DESCR("bitwise negation");
DATA(insert OID = 1685 (  varbitshiftright	PGUID 11 f t t 2 f 1562 "1562 1562" 100 0 1 0  varbitshiftright - ));
DESCR("bitwise right shift");
DATA(insert OID = 1686 (  varbitshiftleft	PGUID 11 f t t 2 f 1562 "1562 1562" 100 0 1 0  varbitshiftleft - ));
DESCR("bitwise left shift");
DATA(insert OID = 1687 (  varbitcat			PGUID 11 f t t 2 f 1562 "1562 1562" 100 0 1 0  varbitcat - ));
DESCR("bitwise concatenation");
DATA(insert OID = 1688 (  varbitsubstr		PGUID 11 f t t 2 f 1562 "1562 1562" 100 0 1 0  varbitsubstr - ));
DESCR("bitwise field");

/* for mac type support */
DATA(insert OID = 436 (  macaddr_in			PGUID 11 f t t 1 f 829 "0" 100 0 0 100	macaddr_in - ));
DESCR("(internal)");
DATA(insert OID = 437 (  macaddr_out		PGUID 11 f t t 1 f 23 "0" 100 0 0 100  macaddr_out - ));
DESCR("(internal)");

DATA(insert OID = 830 (  macaddr_eq		   PGUID 11 f t t 2 f 16 "829 829" 100 0 0 100	macaddr_eq - ));
DESCR("equal");
DATA(insert OID = 831 (  macaddr_lt		   PGUID 11 f t t 2 f 16 "829 829" 100 0 0 100	macaddr_lt - ));
DESCR("less-than");
DATA(insert OID = 832 (  macaddr_le		   PGUID 11 f t t 2 f 16 "829 829" 100 0 0 100	macaddr_le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 833 (  macaddr_gt		   PGUID 11 f t t 2 f 16 "829 829" 100 0 0 100	macaddr_gt - ));
DESCR("greater-than");
DATA(insert OID = 834 (  macaddr_ge		   PGUID 11 f t t 2 f 16 "829 829" 100 0 0 100	macaddr_ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 835 (  macaddr_ne		   PGUID 11 f t t 2 f 16 "829 829" 100 0 0 100	macaddr_ne - ));
DESCR("not equal");
DATA(insert OID = 836 (  macaddr_cmp	   PGUID 11 f t t 2 f 23 "829 829" 100 0 0 100	macaddr_cmp - ));
DESCR("less-equal-greater");
DATA(insert OID = 837 (  macaddr_manuf	   PGUID 11 f t t 1 f 25 "829" 100 0 0 100	macaddr_manuf - ));
DESCR("MAC manufacturer");

/* for inet type support */
DATA(insert OID = 910 (  inet_in			PGUID 11 f t t 1 f 869 "0" 100 0 0 100	inet_in - ));
DESCR("(internal)");
DATA(insert OID = 911 (  inet_out			PGUID 11 f t t 1 f 23 "0" 100 0 0 100  inet_out - ));
DESCR("(internal)");

/* for cidr type support */
DATA(insert OID = 1267 (  cidr_in			PGUID 11 f t t 1 f 650 "0" 100 0 0 100	cidr_in - ));
DESCR("(internal)");
DATA(insert OID = 1427 (  cidr_out			PGUID 11 f t t 1 f 23 "0" 100 0 0 100  cidr_out - ));
DESCR("(internal)");

/* these are used for both inet and cidr */
DATA(insert OID = 920 (  network_eq		   PGUID 11 f t t 2 f 16 "869 869" 100 0 0 100	network_eq - ));
DESCR("equal");
DATA(insert OID = 921 (  network_lt		   PGUID 11 f t t 2 f 16 "869 869" 100 0 0 100	network_lt - ));
DESCR("less-than");
DATA(insert OID = 922 (  network_le		   PGUID 11 f t t 2 f 16 "869 869" 100 0 0 100	network_le - ));
DESCR("less-than-or-equal");
DATA(insert OID = 923 (  network_gt		   PGUID 11 f t t 2 f 16 "869 869" 100 0 0 100	network_gt - ));
DESCR("greater-than");
DATA(insert OID = 924 (  network_ge		   PGUID 11 f t t 2 f 16 "869 869" 100 0 0 100	network_ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 925 (  network_ne		   PGUID 11 f t t 2 f 16 "869 869" 100 0 0 100	network_ne - ));
DESCR("not equal");
DATA(insert OID = 926 (  network_cmp		   PGUID 11 f t t 2 f 23 "869 869" 100 0 0 100	network_cmp - ));
DESCR("less-equal-greater");
DATA(insert OID = 927 (  network_sub		   PGUID 11 f t t 2 f 16 "869 869" 100 0 0 100	network_sub - ));
DESCR("is-subnet");
DATA(insert OID = 928 (  network_subeq		   PGUID 11 f t t 2 f 16 "869 869" 100 0 0 100	network_subeq - ));
DESCR("is-subnet-or-equal");
DATA(insert OID = 929 (  network_sup		   PGUID 11 f t t 2 f 16 "869 869" 100 0 0 100	network_sup - ));
DESCR("is-supernet");
DATA(insert OID = 930 (  network_supeq		   PGUID 11 f t t 2 f 16 "869 869" 100 0 0 100	network_supeq - ));
DESCR("is-supernet-or-equal");

/* inet/cidr versions */
DATA(insert OID = 696 (  netmask				PGUID 11 f t t 1 f 25 "869" 100 0 0 100  network_netmask - ));
DESCR("netmask of address");
DATA(insert OID = 697 (  masklen				PGUID 11 f t t 1 f 23 "869" 100 0 0 100  network_masklen - ));
DESCR("netmask length");
DATA(insert OID = 698 (  broadcast				PGUID 11 f t t 1 f 25 "869" 100 0 0 100  network_broadcast - ));
DESCR("broadcast address");
DATA(insert OID = 699 (  host					PGUID 11 f t t 1 f 25 "869" 100 0 0 100  network_host - ));
DESCR("host address");
DATA(insert OID = 683 (  network				PGUID 11 f t t 1 f 25 "869" 100 0 0 100  network_network - ));
DESCR("network address");

DATA(insert OID =  1691 (  boolle			   PGUID 11 f t t 2 f 16 "16 16" 100 0 0 100  boolle - ));
DESCR("less-than-or-equal");
DATA(insert OID =  1692 (  boolge			   PGUID 11 f t t 2 f 16 "16 16" 100 0 0 100  boolge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 1693 (  btboolcmp			   PGUID 11 f t t 2 f 23 "16 16" 100 0 0 100  btboolcmp - ));
DESCR("btree less-equal-greater");

/* OID's 1700 - 1799 NUMERIC data type */
DATA(insert OID = 1701 ( numeric_in				PGUID 11 f t t 3 f 1700 "0 0 23" 100 0 0 100  numeric_in - ));
DESCR("(internal)");
DATA(insert OID = 1702 ( numeric_out			PGUID 11 f t t 1 f 23 "0" 100 0 0 100  numeric_out - ));
DESCR("(internal)");
DATA(insert OID = 1703 ( numeric				PGUID 11 f t t 2 f 1700 "1700 23" 100 0 0 100  numeric - ));
DESCR("(internal)");
DATA(insert OID = 1704 ( numeric_abs			PGUID 11 f t t 1 f 1700 "1700" 100 0 0 100	numeric_abs - ));
DESCR("absolute value");
DATA(insert OID = 1705 ( abs					PGUID 11 f t t 1 f 1700 "1700" 100 0 0 100	numeric_abs - ));
DESCR("absolute value");
DATA(insert OID = 1706 ( sign					PGUID 11 f t t 1 f 1700 "1700" 100 0 0 100	numeric_sign - ));
DESCR("sign of value");
DATA(insert OID = 1707 ( round					PGUID 11 f t t 2 f 1700 "1700 23" 100 0 0 100  numeric_round - ));
DESCR("value rounded to 'scale'");
DATA(insert OID = 1708 ( round					PGUID 14 f t t 1 f 1700 "1700" 100 0 0 100	"select numeric_round($1,0)" - ));
DESCR("value rounded to 'scale' of zero");
DATA(insert OID = 1709 ( trunc					PGUID 11 f t t 2 f 1700 "1700 23" 100 0 0 100  numeric_trunc - ));
DESCR("value truncated to 'scale'");
DATA(insert OID = 1710 ( trunc					PGUID 14 f t t 1 f 1700 "1700" 100 0 0 100	"select trunc($1,0)" - ));
DESCR("value truncated to 'scale' of zero");
DATA(insert OID = 1711 ( ceil					PGUID 11 f t t 1 f 1700 "1700" 100 0 0 100	numeric_ceil - ));
DESCR("smallest integer >= value");
DATA(insert OID = 1712 ( floor					PGUID 11 f t t 1 f 1700 "1700" 100 0 0 100	numeric_floor - ));
DESCR("largest integer <= value");
DATA(insert OID = 1718 ( numeric_eq				PGUID 11 f t t 2 f 16 "1700 1700" 100 0 0 100  numeric_eq - ));
DESCR("equal");
DATA(insert OID = 1719 ( numeric_ne				PGUID 11 f t t 2 f 16 "1700 1700" 100 0 0 100  numeric_ne - ));
DESCR("not equal");
DATA(insert OID = 1720 ( numeric_gt				PGUID 11 f t t 2 f 16 "1700 1700" 100 0 0 100  numeric_gt - ));
DESCR("greater-than");
DATA(insert OID = 1721 ( numeric_ge				PGUID 11 f t t 2 f 16 "1700 1700" 100 0 0 100  numeric_ge - ));
DESCR("greater-than-or-equal");
DATA(insert OID = 1722 ( numeric_lt				PGUID 11 f t t 2 f 16 "1700 1700" 100 0 0 100  numeric_lt - ));
DESCR("lower-than");
DATA(insert OID = 1723 ( numeric_le				PGUID 11 f t t 2 f 16 "1700 1700" 100 0 0 100  numeric_le - ));
DESCR("lower-than-or-equal");
DATA(insert OID = 1724 ( numeric_add			PGUID 11 f t t 2 f 1700 "1700 1700" 100 0 0 100  numeric_add - ));
DESCR("addition");
DATA(insert OID = 1725 ( numeric_sub			PGUID 11 f t t 2 f 1700 "1700 1700" 100 0 0 100  numeric_sub - ));
DESCR("subtract");
DATA(insert OID = 1726 ( numeric_mul			PGUID 11 f t t 2 f 1700 "1700 1700" 100 0 0 100  numeric_mul - ));
DESCR("multiply");
DATA(insert OID = 1727 ( numeric_div			PGUID 11 f t t 2 f 1700 "1700 1700" 100 0 0 100  numeric_div - ));
DESCR("divide");
DATA(insert OID = 1728 ( mod					PGUID 11 f t t 2 f 1700 "1700 1700" 100 0 0 100  numeric_mod - ));
DESCR("modulus");
DATA(insert OID = 1729 ( numeric_mod			PGUID 11 f t t 2 f 1700 "1700 1700" 100 0 0 100  numeric_mod - ));
DESCR("modulus");
DATA(insert OID = 1730 ( sqrt					PGUID 11 f t t 1 f 1700 "1700" 100 0 0 100	numeric_sqrt - ));
DESCR("square root");
DATA(insert OID = 1731 ( numeric_sqrt			PGUID 11 f t t 1 f 1700 "1700" 100 0 0 100	numeric_sqrt - ));
DESCR("square root");
DATA(insert OID = 1732 ( exp					PGUID 11 f t t 1 f 1700 "1700" 100 0 0 100	numeric_exp - ));
DESCR("e raised to the power of n");
DATA(insert OID = 1733 ( numeric_exp			PGUID 11 f t t 1 f 1700 "1700" 100 0 0 100	numeric_exp - ));
DESCR("e raised to the power of n");
DATA(insert OID = 1734 ( ln						PGUID 11 f t t 1 f 1700 "1700" 100 0 0 100	numeric_ln - ));
DESCR("natural logarithm of n");
DATA(insert OID = 1735 ( numeric_ln				PGUID 11 f t t 1 f 1700 "1700" 100 0 0 100	numeric_ln - ));
DESCR("natural logarithm of n");
DATA(insert OID = 1736 ( log					PGUID 11 f t t 2 f 1700 "1700 1700" 100 0 0 100  numeric_log - ));
DESCR("logarithm base m of n");
DATA(insert OID = 1737 ( numeric_log			PGUID 11 f t t 2 f 1700 "1700 1700" 100 0 0 100  numeric_log - ));
DESCR("logarithm base m of n");
DATA(insert OID = 1738 ( pow					PGUID 11 f t t 2 f 1700 "1700 1700" 100 0 0 100  numeric_power - ));
DESCR("m raised to the power of n");
DATA(insert OID = 1739 ( numeric_power			PGUID 11 f t t 2 f 1700 "1700 1700" 100 0 0 100  numeric_power - ));
DESCR("m raised to the power of n");
DATA(insert OID = 1740 ( numeric				PGUID 11 f t t 1 f 1700 "23" 100 0 0 100  int4_numeric - ));
DESCR("(internal)");
DATA(insert OID = 1741 ( log					PGUID 14 f t t 1 f 1700 "1700" 100 0 0 100	"select log(10, $1)" - ));
DESCR("logarithm base 10 of n");
DATA(insert OID = 1742 ( numeric				PGUID 11 f t t 1 f 1700 "700" 100 0 0 100  float4_numeric - ));
DESCR("(internal)");
DATA(insert OID = 1743 ( numeric				PGUID 11 f t t 1 f 1700 "701" 100 0 0 100  float8_numeric - ));
DESCR("(internal)");
DATA(insert OID = 1744 ( int4					PGUID 11 f t t 1 f 23 "1700" 100 0 0 100  numeric_int4 - ));
DESCR("(internal)");
DATA(insert OID = 1745 ( float4					PGUID 11 f t t 1 f 700 "1700" 100 0 0 100  numeric_float4 - ));
DESCR("(internal)");
DATA(insert OID = 1746 ( float8					PGUID 11 f t t 1 f 701 "1700" 100 0 0 100  numeric_float8 - ));
DESCR("(internal)");
DATA(insert OID = 1764 ( numeric_inc			PGUID 11 f t t 1 f 1700 "1700" 100 0 0 100	numeric_inc - ));
DESCR("increment by one");
DATA(insert OID = 1765 ( numeric_dec			PGUID 11 f t t 1 f 1700 "1700" 100 0 0 100	numeric_dec - ));
DESCR("decrement by one");
DATA(insert OID = 1766 ( numeric_smaller		PGUID 11 f t t 2 f 1700 "1700 1700" 100 0 0 100  numeric_smaller - ));
DESCR("smaller of two numbers");
DATA(insert OID = 1767 ( numeric_larger			PGUID 11 f t t 2 f 1700 "1700 1700" 100 0 0 100  numeric_larger - ));
DESCR("larger of two numbers");
DATA(insert OID = 1769 ( numeric_cmp			PGUID 11 f t t 2 f 23 "1700 1700" 100 0 0 100  numeric_cmp - ));
DESCR("compare two numbers");
DATA(insert OID = 1771 ( numeric_uminus			PGUID 11 f t t 1 f 1700 "1700" 100 0 0 100	numeric_uminus - ));
DESCR("negate");
DATA(insert OID = 1779 ( int8					PGUID 11 f t t 1 f 20 "1700" 100 0 0 100  numeric_int8 - ));
DESCR("(internal)");
DATA(insert OID = 1781 ( numeric				PGUID 11 f t t 1 f 1700 "20" 100 0 0 100  int8_numeric - ));
DESCR("(internal)");
DATA(insert OID = 1782 ( numeric				PGUID 11 f t t 1 f 1700 "21" 100 0 0 100  int2_numeric - ));
DESCR("(internal)");
DATA(insert OID = 1783 ( int2					PGUID 11 f t t 1 f 21 "1700" 100 0 0 100  numeric_int2 - ));
DESCR("(internal)");

/* formatting */
DATA(insert OID = 1770 ( to_char			PGUID 11 f t f 2 f	25 "1184 25" 100 0 0 100  timestamp_to_char - ));
DESCR("format timestamp to text");
DATA(insert OID = 1772 ( to_char			PGUID 11 f t f 2 f	25 "1700 25" 100 0 0 100  numeric_to_char - ));
DESCR("format numeric to text");
DATA(insert OID = 1773 ( to_char			PGUID 11 f t f 2 f	25 "23 25" 100 0 0 100	int4_to_char - ));
DESCR("format int4 to text");
DATA(insert OID = 1774 ( to_char			PGUID 11 f t f 2 f	25 "20 25" 100 0 0 100	int8_to_char - ));
DESCR("format int8 to text");
DATA(insert OID = 1775 ( to_char			PGUID 11 f t f 2 f	25 "700 25" 100 0 0 100  float4_to_char - ));
DESCR("format float4 to text");
DATA(insert OID = 1776 ( to_char			PGUID 11 f t f 2 f	25 "701 25" 100 0 0 100  float8_to_char - ));
DESCR("format float8 to text");
DATA(insert OID = 1777 ( to_number			PGUID 11 f t f 2 f	1700 "25 25" 100 0 0 100  numeric_to_number - ));
DESCR("convert text to numeric");
DATA(insert OID = 1778 ( to_timestamp		PGUID 11 f t f 2 f	1184 "25 25" 100 0 0 100  to_timestamp - ));
DESCR("convert text to timestamp");
DATA(insert OID = 1780 ( to_date			PGUID 11 f t f 2 f	1082 "25 25" 100 0 0 100  to_date - ));
DESCR("convert text to date");

/* Selectivity estimators for LIKE and related operators */
DATA(insert OID = 1818 ( regexeqsel			PGUID 11 f t f 5 f 701 "26 26 21 0 23" 100 0 0 100  regexeqsel - ));
DESCR("restriction selectivity of regex match");
DATA(insert OID = 1819 ( likesel			PGUID 11 f t f 5 f 701 "26 26 21 0 23" 100 0 0 100  likesel - ));
DESCR("restriction selectivity of LIKE");
DATA(insert OID = 1820 ( icregexeqsel		PGUID 11 f t f 5 f 701 "26 26 21 0 23" 100 0 0 100  icregexeqsel - ));
DESCR("restriction selectivity of case-insensitive regex match");
DATA(insert OID = 1821 ( regexnesel			PGUID 11 f t f 5 f 701 "26 26 21 0 23" 100 0 0 100  regexnesel - ));
DESCR("restriction selectivity of regex non-match");
DATA(insert OID = 1822 ( nlikesel			PGUID 11 f t f 5 f 701 "26 26 21 0 23" 100 0 0 100  nlikesel - ));
DESCR("restriction selectivity of NOT LIKE");
DATA(insert OID = 1823 ( icregexnesel		PGUID 11 f t f 5 f 701 "26 26 21 0 23" 100 0 0 100  icregexnesel - ));
DESCR("restriction selectivity of case-insensitive regex non-match");
DATA(insert OID = 1824 ( regexeqjoinsel		PGUID 11 f t f 5 f 701 "26 26 21 26 21" 100 0 0 100	regexeqjoinsel - ));
DESCR("join selectivity of regex match");
DATA(insert OID = 1825 ( likejoinsel		PGUID 11 f t f 5 f 701 "26 26 21 26 21" 100 0 0 100	likejoinsel - ));
DESCR("join selectivity of LIKE");
DATA(insert OID = 1826 ( icregexeqjoinsel	PGUID 11 f t f 5 f 701 "26 26 21 26 21" 100 0 0 100	icregexeqjoinsel - ));
DESCR("join selectivity of case-insensitive regex match");
DATA(insert OID = 1827 ( regexnejoinsel		PGUID 11 f t f 5 f 701 "26 26 21 26 21" 100 0 0 100	regexnejoinsel - ));
DESCR("join selectivity of regex non-match");
DATA(insert OID = 1828 ( nlikejoinsel		PGUID 11 f t f 5 f 701 "26 26 21 26 21" 100 0 0 100	nlikejoinsel - ));
DESCR("join selectivity of NOT LIKE");
DATA(insert OID = 1829 ( icregexnejoinsel	PGUID 11 f t f 5 f 701 "26 26 21 26 21" 100 0 0 100	icregexnejoinsel - ));
DESCR("join selectivity of case-insensitive regex non-match");

/*  connector conversions  */
DATA(insert OID = 1801 ( connectortoint4	PGUID 11 f t f 1 f 23 "1136" 100 0 0 100	connectortoint4 - ));
DESCR("connector to int4");
DATA(insert OID = 1802 ( int4toconnector	PGUID 11 f t f 1 f 1136 "23" 100 0 0 100	int4toconnector - ));
DESCR("int4 to connector");

/*  java functions  */
DATA(insert OID = 1999 ( java_instanceof	PGUID 11 f t f 2 f 16 "1830 1043" 100 0 0 100	java_instanceof - ));
DATA(insert OID = 2010 ( java_compare	PGUID 11 f t f 2 f 23 "1830 1830" 100 0 0 100	java_compare - ));
DATA(insert OID = 2011 ( java_equals	PGUID 11 f t f 2 f 16 "1830 1830" 100 0 0 100	java_equals - ));
DATA(insert OID = 2012 ( java_lt	PGUID 11 f t f 2 f 16 "1830 1830" 100 0 0 100	java_lt - ));
DATA(insert OID = 2013 ( java_gt	PGUID 11 f t f 2 f 16 "1830 1830" 100 0 0 100	java_gt - ));
DATA(insert OID = 2014 ( java_lteq	PGUID 11 f t f 2 f 16 "1830 1830" 100 0 0 100	java_lteq - ));
DATA(insert OID = 2015 ( java_gteq	PGUID 11 f t f 2 f 16 "1830 1830" 100 0 0 100	java_gteq - ));

DATA(insert OID = 2016 ( javatextin	PGUID 11 f t f 1 f 1830 "0" 100 0 0 100	javatextin - ));
DATA(insert OID = 2017 ( javatextout	PGUID 11 f t f 1 f 25 "0" 100 0 0 100	javatextout - ));

DATA(insert OID = 2018 ( java_noteq	PGUID 11 f t f 2 f 16 "1830 1830" 100 0 0 100	java_noteq - ));

DATA(insert OID =  1831 (  javain		   PGUID 11 f t t 1 f 1830 "17" 100 0 0 100  javain - ));
#define JAVAINOID 1831
DESCR("(internal)");
DATA(insert OID =  1832 (  javaout		   PGUID 11 f t t 1 f 17 "1830" 100 0 0 100  javaout - ));
#define JAVAOUTOID 1832
DESCR("(internal)");

DATA(insert OID =  1838 (  wrappedin		   PGUID 11 f t t 1 f 1837 "17" 100 0 0 100  wrappedin - ));
#define WRAPPEDINOID 1838
DESCR("(internal)");
DATA(insert OID =  1839 (  wrappedout		   PGUID 11 f t t 1 f 17 "1837" 100 0 0 100  wrappedout - ));
#define WRAPPEDOUTOID 1839
DESCR("(internal)");
DATA(insert OID =  1840 (  wrappedtotext		   PGUID 11 f t t 1 f 25 "1837" 100 0 0 100  wrappedtotext - ));
#define WRAPPEDTOTEXTOID 1840
DESCR("(internal)");

DATA(insert OID = 2019 (  length			 PGUID 11 f t t 1 f   23 "1830" 100 0 0 100  javalen - ));

DATA(insert OID = 2111 ( xidint8_equals	PGUID 11 f t f 2 f 16 "28 20" 100 0 0 100	xidint8_equals - ));
DATA(insert OID = 2112 ( xidint8_lt	PGUID 11 f t f 2 f 16 "28 20" 100 0 0 100	xidint8_lt - ));
DATA(insert OID = 2113 ( xidint8_gt	PGUID 11 f t f 2 f 16 "28 20" 100 0 0 100	xidint8_gt - ));
DATA(insert OID = 2114 ( xidint8_lteq	PGUID 11 f t f 2 f 16 "28 20" 100 0 0 100	xidint8_lteq - ));
DATA(insert OID = 2115 ( xidint8_gteq	PGUID 11 f t f 2 f 16 "28 20" 100 0 0 100	xidint8_gteq - ));
DATA(insert OID = 2118 ( xidint8_noteq	PGUID 11 f t f 2 f 16 "28 20" 100 0 0 100	xidint8_noteq - ));

DATA(insert OID = 2211 ( xidint4_equals	PGUID 11 f t f 2 f 16 "28 20" 100 0 0 100	xidint4_equals - ));
DATA(insert OID = 2212 ( xidint4_lt	PGUID 11 f t f 2 f 16 "28 20" 100 0 0 100	xidint4_lt - ));
DATA(insert OID = 2213 ( xidint4_gt	PGUID 11 f t f 2 f 16 "28 20" 100 0 0 100	xidint4_gt - ));
DATA(insert OID = 2214 ( xidint4_lteq	PGUID 11 f t f 2 f 16 "28 20" 100 0 0 100	xidint4_lteq - ));
DATA(insert OID = 2215 ( xidint4_gteq	PGUID 11 f t f 2 f 16 "28 20" 100 0 0 100	xidint4_gteq - ));
DATA(insert OID = 2218 ( xidint4_noteq	PGUID 11 f t f 2 f 16 "28 20" 100 0 0 100	xidint4_noteq - ));


/*
 * prototypes for functions pg_proc.c
 */
extern Oid ProcedureCreate(char *procedureName,
				bool returnsSet,
				char *returnTypeName,
				char *languageName,
				char *prosrc,
				char *probin,
				bool canCache,
				bool trusted,
				int32 byte_pct,
				int32 perbyte_cpu,
				int32 percall_cpu,
				int32 outin_ratio,
				List *argList);


#endif	 /* PG_PROC_H */
