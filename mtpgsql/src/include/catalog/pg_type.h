/*-------------------------------------------------------------------------
 *
 * pg_type.h
 *	  definition of the system "type" relation (pg_type)
 *	  along with the relation's initial contents.
 *
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 * NOTES
 *	  the genbki.sh script reads this file and generates .bki
 *	  information from the DATA() statements.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_TYPE_H
#define PG_TYPE_H

/* ----------------
 *		postgres.h contains the system type definitions and the
 *		CATALOG(), BOOTSTRAP and DATA() sugar words so this file
 *		can be read by both genbki.sh and the C compiler.
 * ----------------
 */

/* ----------------
 *		pg_type definition.  cpp turns this into
 *		typedef struct FormData_pg_type
 *
 *		Some of the values in a pg_type instance are copied into
 *		pg_attribute instances.  Some parts of Postgres use the pg_type copy,
 *		while others use the pg_attribute copy, so they must match.
 *		See struct FormData_pg_attribute for details.
 * ----------------
 */
CATALOG(pg_type) BOOTSTRAP
{
	NameData	typname;
	int4		typowner;
	int2		typlen;

	/*
	 * typlen is the number of bytes we use to represent a value of this
	 * type, e.g. 4 for an int4.  But for a variable length type, typlen
	 * is -1.
	 */
	int2		typprtlen;

	/*
	 * typbyval determines whether internal Postgres routines pass a value
	 * of this type by value or by reference.  Only char, short, and int-
	 * equivalent items can be passed by value, so if the type is not 1,
	 * 2, or 4 bytes long, Postgres does not have the option of passing by
	 * value and so typbyval had better be FALSE.  Variable-length types
	 * are always passed by reference. Note that typbyval can be false
	 * even if the length would allow pass-by-value; this is currently
	 * true for type float4, for example.
	 */
	bool		typbyval;
	char		typtype;

	/*
	 * typtype is 'b' for a basic type and 'c' for a catalog type (ie a
	 * class). If typtype is 'c', typrelid is the OID of the class' entry
	 * in pg_class. (Why do we need an entry in pg_type for classes,
	 * anyway?)
	 */
	bool		typisdefined;
	char		typdelim;
	Oid			typrelid;		/* 0 if not a class type */
	Oid			typelem;

	/*
	 * typelem is 0 if this is not an array type.  If this is an array
	 * type, typelem is the OID of the type of the elements of the array
	 * (it identifies another row in Table pg_type).
	 */
	regproc		typinput;
	regproc		typoutput;
	regproc		typreceive;
	regproc		typsend;
	char		typalign;

	/* ----------------
	 * typalign is the alignment required when storing a value of this
	 * type.  It applies to storage on disk as well as most
	 * representations of the value inside Postgres.  When multiple values
	 * are stored consecutively, such as in the representation of a
	 * complete row on disk, padding is inserted before a datum of this
	 * type so that it begins on the specified boundary.  The alignment
	 * reference is the beginning of the first datum in the sequence.
	 *
	 * 'c' = CHAR alignment, ie no alignment needed.
	 * 's' = SHORT alignment (2 bytes on most machines).
	 * 'i' = INT alignment (4 bytes on most machines).
	 * 'd' = DOUBLE alignment (8 bytes on many machines, but by no means all).
	 *
	 * See include/utils/memutils.h for the macros that compute these
	 * alignment requirements.
	 *
	 * NOTE: for types used in system tables, it is critical that the
	 * size and alignment defined in pg_type agree with the way that the
	 * compiler will lay out the field in a struct representing a table row.
	 * ----------------
	 */
	text		typdefault;		/* VARIABLE LENGTH FIELD */
} FormData_pg_type;

/* ----------------
 *		Form_pg_type corresponds to a pointer to a row with
 *		the format of pg_type relation.
 * ----------------
 */
typedef FormData_pg_type *Form_pg_type;

/* ----------------
 *		compiler constants for pg_type
 * ----------------
 */
#define Natts_pg_type					16
#define Anum_pg_type_typname			1
#define Anum_pg_type_typowner			2
#define Anum_pg_type_typlen				3
#define Anum_pg_type_typprtlen			4
#define Anum_pg_type_typbyval			5
#define Anum_pg_type_typtype			6
#define Anum_pg_type_typisdefined		7
#define Anum_pg_type_typdelim			8
#define Anum_pg_type_typrelid			9
#define Anum_pg_type_typelem			10
#define Anum_pg_type_typinput			11
#define Anum_pg_type_typoutput			12
#define Anum_pg_type_typreceive			13
#define Anum_pg_type_typsend			14
#define Anum_pg_type_typalign			15
#define Anum_pg_type_typdefault			16

/* ----------------
 *		initial contents of pg_type
 * ----------------
 */
 

/* keep the following ordered by OID so that later changes can be made easier*/

/* Make sure the typlen, typbyval, and typalign values here match the initial
   values for attlen, attbyval, and attalign in both places in pg_attribute.h
   for every instance.
*/

/* OIDS 1 - 99 */
DATA(insert OID = 16 (	bool	   PGUID  1   1 t b t \x2C 0   0 boolin boolout boolin boolout c _null_ ));
DESCR("boolean, 'true'/'false'");
#define BOOLOID			16

DATA(insert OID = 17 (	bytea	   PGUID -1  -1 f b t \x2C 0  18 byteain byteaout byteain byteaout i _null_ ));
DESCR("variable-length string, binary values escaped");
#define BYTEAOID		17

DATA(insert OID = 18 (	char	   PGUID  1   1 t b t \x2C 0   0 charin charout charin charout c _null_ ));
DESCR("single character");
#define CHAROID			18

DATA(insert OID = 1841 (	schar	   PGUID  1   1 t b t \x2C 0   0 charin charout charin charout c _null_ ));
DESCR("single character");

DATA(insert OID = 19 (	name	   PGUID NAMEDATALEN NAMEDATALEN  f b t \x2C 0	18 namein nameout namein nameout i _null_ ));
DESCR("64-character type for storing system identifiers");
#define NAMEOID			19

DATA(insert OID = 20 (	int8	   PGUID  8  20 f b t \x2C 0   0 int8in int8out int8in int8out d _null_ ));
DESCR("~18 digit integer, 8-byte storage");
#define INT8OID			20

DATA(insert OID = 21 (	int2	   PGUID  2   5 t b t \x2C 0   0 int2in int2out int2in int2out s _null_ ));
DESCR("-32 thousand to 32 thousand, 2-byte storage");
#define INT2OID			21

DATA(insert OID = 22 (	int2vector PGUID INDEX_MAX_KEYS*2 -1 f b t \x2C 0  21 int2vectorin int2vectorout int2vectorin int2vectorout i _null_ ));
DESCR("array of INDEX_MAX_KEYS int2 integers, used in system tables");
#define INT2VECTOROID	22

DATA(insert OID = 23 (	int4	   PGUID  4  10 t b t \x2C 0   0 int4in int4out int4in int4out i _null_ ));
DESCR("-2 billion to 2 billion integer, 4-byte storage");
#define INT4OID			23

DATA(insert OID = 1136 (	connector	   PGUID  4  10 t b t \x2C 0   0 int4in int4out int4in int4out i _null_ ));
DESCR("-2 billion to 2 billion integer, 4-byte storage");
#define CONNECTOROID			1136

DATA(insert OID = 24 (	regproc    PGUID  OIDSIZE  16 t b t \x2C 0   0 regprocin regprocout regprocin regprocout l _null_ ));
DESCR("registered procedure");
#define REGPROCOID		24

DATA(insert OID = 25 (	text	   PGUID -1  -1 f b t \x2C 0  18 textin textout textin textout i _null_ ));
DESCR("variable-length string, no limit specified");
#define TEXTOID			25

DATA(insert OID = 1803 (	blob	   PGUID -1  -1 f b t \x2C 0  0 textin textout textin textout i _null_ ));
DESCR("binary data that can span pages");
#define BLOBOID		1803

DATA(insert OID = 1834 (	streaming	   PGUID -1  -1 f b t \x2C 0  0 textin textout textin textout i _null_ ));
DESCR("streaming psuedo type data for the streaming of blobs");
#define STREAMINGOID   1834   /*  streaming psuedotype for BLOBS  */

DATA(insert OID = 1830 (	java	   PGUID -1  -1 f b t \x2C 0  0 javatextin javatextout - - i _null_ ));
DESCR("java data that can span pages");
#define JAVAOID	 1830

DATA(insert OID = 1837 (	wrapped	   PGUID -1  -1 f b t \x2C 0  0 - wrappedtotext - - l _null_ ));
DESCR("wrapped data");
#define WRAPPEDOID			1837

DATA(insert OID = 26 (	oid		   PGUID  OIDSIZE  10 t b t \x2C 0   0 oidin oidout oidin oidout l _null_ ));
DESCR("object identifier(oid), maximum 4 billion");
#define OIDOID			26

DATA(insert OID = 27 (	tid		   PGUID  TIDSIZE  19 f b t \x2C 0   0 tidin tidout tidin tidout l _null_ ));
DESCR("(Block, offset), physical location of tuple");
#define TIDOID		27

DATA(insert OID = 28 (	xid		   PGUID  8  12 f b t \x2C 0   0 xidin xidout xidin xidout d _null_ ));
DESCR("transaction id");
#define XIDOID 28

DATA(insert OID = 29 (	cid		   PGUID  4  10 t b t \x2C 0   0 cidin cidout cidin cidout i _null_ ));
DESCR("command identifier type, sequence in transaction id");
#define CIDOID 29

DATA(insert OID = 30 (	oidvector  PGUID OIDARRAYSIZE -1 f b t \x2C 0  26 oidvectorin oidvectorout oidvectorin oidvectorout l _null_ ));
DESCR("array of INDEX_MAX_KEYS oids, used in system tables");
#define OIDVECTOROID	30

DATA(insert OID = 32 (	SET		   PGUID -1  -1 f b t \x2C 0   0 textin textout textin textout i _null_ ));
DESCR("set of tuples");

DATA(insert OID = 71 (	pg_type		 PGUID OIDSIZE OIDSIZE t c t \x2C 1247 0 foo bar foo bar l _null_ ));
DATA(insert OID = 75 (	pg_attribute PGUID OIDSIZE OIDSIZE t c t \x2C 1249 0 foo bar foo bar l _null_ ));
DATA(insert OID = 81 (	pg_proc		 PGUID OIDSIZE OIDSIZE t c t \x2C 1255 0 foo bar foo bar l _null_ ));
DATA(insert OID = 83 (	pg_class	 PGUID OIDSIZE OIDSIZE t c t \x2C 1259 0 foo bar foo bar l _null_ ));
DATA(insert OID = 86 (	pg_shadow	 PGUID OIDSIZE OIDSIZE t c t \x2C 1260 0 foo bar foo bar l _null_ ));
DATA(insert OID = 87 (	pg_group	 PGUID OIDSIZE OIDSIZE t c t \x2C 1261 0 foo bar foo bar l _null_ ));
DATA(insert OID = 88 (	pg_database  PGUID OIDSIZE OIDSIZE t c t \x2C 1262 0 foo bar foo bar l _null_ ));
DATA(insert OID = 964 (	pg_schema  PGUID OIDSIZE OIDSIZE t c t \x2C 1628 0 foo bar foo bar l _null_ ));
DATA(insert OID = 90 (	pg_variable  PGUID OIDSIZE OIDSIZE t c t \x2C 1264 0 foo bar foo bar l _null_ ));
DATA(insert OID = 99 (	pg_log		 PGUID OIDSIZE OIDSIZE t c t \x2C 1269 0 foo bar foo bar l _null_ ));

/* OIDS 100 - 199 */

DATA(insert OID = 109 (  pg_attrdef  PGUID OIDSIZE OIDSIZE t c t \x2C 1215 0 foo bar foo bar l _null_ ));
DATA(insert OID = 110 (  pg_relcheck PGUID OIDSIZE OIDSIZE t c t \x2C 1216 0 foo bar foo bar l _null_ ));
DATA(insert OID = 111 (  pg_trigger  PGUID OIDSIZE OIDSIZE t c t \x2C 1219 0 foo bar foo bar l _null_ ));
DATA(insert OID = 1836 (  pg_extent  PGUID OIDSIZE OIDSIZE t c t \x2C 1835 0 foo bar foo bar l _null_ ));

/* OIDS 200 - 299 */

DATA(insert OID = 210 (  smgr	   PGUID 2	12 t b t \x2C 0 0 smgrin smgrout smgrin smgrout s _null_ ));
DESCR("storage manager");

/* OIDS 300 - 399 */

/* OIDS 400 - 499 */

/* OIDS 500 - 599 */

/* OIDS 600 - 699 */
DATA(insert OID = 600 (  point	   PGUID 16  24 f b t \x2C 0 701 point_in point_out point_in point_out d _null_ ));
DESCR("geometric point '(x, y)'");
#define POINTOID		600
DATA(insert OID = 601 (  lseg	   PGUID 32  48 f b t \x2C 0 600 lseg_in lseg_out lseg_in lseg_out d _null_ ));
DESCR("geometric line segment '(pt1,pt2)'");
#define LSEGOID			601
DATA(insert OID = 602 (  path	   PGUID -1  -1 f b t \x2C 0 600 path_in path_out path_in path_out d _null_ ));
DESCR("geometric path '(pt1,...)'");
#define PATHOID			602
DATA(insert OID = 603 (  rect	   PGUID 32 100 f b t \x3B 0 600 rect_in rect_out rect_in rect_out d _null_ ));
DESCR("geometric box '(lower left,upper right)'");
#define BOXOID			603
DATA(insert OID = 604 (  polygon   PGUID -1  -1 f b t \x2C 0   0 poly_in poly_out poly_in poly_out d _null_ ));
DESCR("geometric polygon '(pt1,...)'");
#define POLYGONOID		604
DATA(insert OID = 605 (  filename  PGUID 256 -1 f b t \x2C 0  18 filename_in filename_out filename_in filename_out i _null_ ));
DESCR("filename used in system tables");

DATA(insert OID = 628 (  line	   PGUID 32  48 f b t \x2C 0 701 line_in line_out line_in line_out d _null_ ));
DESCR("geometric line '(pt1,pt2)'");
#define LINEOID			628

#ifndef NOARRAY
DATA(insert OID = 629 (  _line	   PGUID  -1 -1 f b t \x2C 0 628 array_in array_out array_in array_out d _null_ ));
DESCR("");
#endif
/* OIDS 700 - 799 */

DATA(insert OID = 700 (  float4    PGUID  4  12 f b t \x2C 0   0 float4in float4out float4in float4out i _null_ ));
DESCR("single-precision floating point number, 4-byte storage");
#define FLOAT4OID 700
DATA(insert OID = 701 (  float8    PGUID  8  24 f b t \x2C 0   0 float8in float8out float8in float8out d _null_ ));
DESCR("double-precision floating point number, 8-byte storage");
#define FLOAT8OID 701
DATA(insert OID = 702 (  abstime   PGUID  4  20 t b t \x2C 0   0 nabstimein nabstimeout nabstimein nabstimeout i _null_ ));
DESCR("absolute, limited-range date and time (Unix system time)");
#define ABSTIMEOID		702
DATA(insert OID = 703 (  reltime   PGUID  4  20 t b t \x2C 0   0 reltimein reltimeout reltimein reltimeout i _null_ ));
DESCR("relative, limited-range time interval (Unix delta time)");
#define RELTIMEOID		703
DATA(insert OID = 704 (  tinterval PGUID 12  47 f b t \x2C 0   0 tintervalin tintervalout tintervalin tintervalout i _null_ ));
DESCR("(abstime,abstime), time interval");
#define TINTERVALOID	704
DATA(insert OID = 705 (  unknown   PGUID -1  -1 f b t \x2C 0   18 textin textout textin textout i _null_ ));
DESCR("");
#define UNKNOWNOID		705

DATA(insert OID = 718 (  circle    PGUID  24 47 f b t \x2C 0	0 circle_in circle_out circle_in circle_out d _null_ ));
DESCR("geometric circle '(center,radius)'");
#define CIRCLEOID		718

#ifndef NOARRAY
DATA(insert OID = 719 (  _circle   PGUID  -1 -1 f b t \x2C 0  718 array_in array_out array_in array_out d _null_ ));
#endif
DATA(insert OID = 790 (  money	   PGUID   4 24 f b t \x2C 0	0 cash_in cash_out cash_in cash_out i _null_ ));
DESCR("$d,ddd.cc, money");
#define CASHOID 790

#ifndef NOARRAY
DATA(insert OID = 791 (  _money    PGUID  -1 -1 f b t \x2C 0  790 array_in array_out array_in array_out i _null_ ));
#endif
/* OIDS 800 - 899 */
DATA(insert OID = 829 ( macaddr    PGUID  6 -1 f b t \x2C 0 0 macaddr_in macaddr_out macaddr_in macaddr_out i _null_ ));
DESCR("XX:XX:XX:XX:XX, MAC address");
DATA(insert OID = 869 ( inet	   PGUID  -1 -1 f b t \x2C 0 0 inet_in inet_out inet_in inet_out i _null_ ));
DESCR("IP address/netmask, host address, netmask optional");
#define INETOID 869
DATA(insert OID = 650 ( cidr	   PGUID  -1 -1 f b t \x2C 0 0 cidr_in cidr_out cidr_in cidr_out i _null_ ));
DESCR("network IP address/netmask, network address");
#define CIDROID 650

/* OIDS 900 - 999 */
DATA(insert OID = 952 (	long		PGUID  LONGSIZE  LONGSIZE t b t \x2C 0   0 longin longout longin longout l _null_ ));
DESCR("platform specific long");
#define LONGOID			952

#ifndef NOARRAY
/* OIDS 1000 - 1099 */
DATA(insert OID = 1000 (  _bool		 PGUID -1  -1 f b t \x2C 0	16 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1001 (  _bytea	 PGUID -1  -1 f b t \x2C 0	17 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1002 (  _char		 PGUID -1  -1 f b t \x2C 0	18 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1003 (  _name		 PGUID -1  -1 f b t \x2C 0	19 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1005 (  _int2		 PGUID -1  -1 f b t \x2C 0	21 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1006 (  _int2vector PGUID -1	-1 f b t \x2C 0 22 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1007 (  _int4		 PGUID -1  -1 f b t \x2C 0	23 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1008 (  _regproc	 PGUID -1  -1 f b t \x2C 0	24 array_in array_out array_in array_out l _null_ ));
DATA(insert OID = 1009 (  _text		 PGUID -1  -1 f b t \x2C 0	25 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1028 (  _oid		 PGUID -1  -1 f b t \x2C 0	26 array_in array_out array_in array_out l _null_ ));
DATA(insert OID = 1010 (  _tid		 PGUID -1  -1 f b t \x2C 0	27 array_in array_out array_in array_out l _null_ ));
DATA(insert OID = 1011 (  _xid		 PGUID -1  -1 f b t \x2C 0	28 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1012 (  _cid		 PGUID -1  -1 f b t \x2C 0	29 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1013 (  _oidvector PGUID -1  -1 f b t \x2C 0	30 array_in array_out array_in array_out l _null_ ));
DATA(insert OID = 1014 (  _bpchar	 PGUID -1  -1 f b t \x2C 0 1042 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1015 (  _varchar	 PGUID -1  -1 f b t \x2C 0 1043 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1016 (  _int8		 PGUID -1  -1 f b t \x2C 0	20 array_in array_out array_in array_out d _null_ ));
DATA(insert OID = 1017 (  _point	 PGUID -1  -1 f b t \x2C 0 600 array_in array_out array_in array_out d _null_ ));
DATA(insert OID = 1018 (  _lseg		 PGUID -1  -1 f b t \x2C 0 601 array_in array_out array_in array_out d _null_ ));
DATA(insert OID = 1019 (  _path		 PGUID -1  -1 f b t \x2C 0 602 array_in array_out array_in array_out d _null_ ));
DATA(insert OID = 1020 (  _rect		 PGUID -1  -1 f b t \x3B 0 603 array_in array_out array_in array_out d _null_ ));
DATA(insert OID = 1021 (  _float4	 PGUID -1  -1 f b t \x2C 0 700 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1022 (  _float8	 PGUID -1  -1 f b t \x2C 0 701 array_in array_out array_in array_out d _null_ ));
DATA(insert OID = 1023 (  _abstime	 PGUID -1  -1 f b t \x2C 0 702 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1024 (  _reltime	 PGUID -1  -1 f b t \x2C 0 703 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1025 (  _tinterval PGUID -1  -1 f b t \x2C 0 704 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1026 (  _filename  PGUID -1  -1 f b t \x2C 0 605 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1027 (  _polygon	 PGUID -1  -1 f b t \x2C 0 604 array_in array_out array_in array_out d _null_ ));
#endif
/*
 *	Note: the size of aclitem needs to match sizeof(AclItem) in acl.h.
 *	Thanks to some padding, this will be 8 on all platforms.
 *	We also have an Assert to make sure.
 */

#define ACLITEMSIZE 8
DATA(insert OID = 1033 (  aclitem	 PGUID 8   -1 f b t \x2C 0 0 aclitemin aclitemout aclitemin aclitemout i _null_ ));
DESCR("access control list");

#ifndef NOARRAY
DATA(insert OID = 1034 (  _aclitem	 PGUID -1 -1 f b t \x2C 0 1033 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1040 (  _macaddr	 PGUID -1 -1 f b t \x2C 0  829 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1041 (  _inet    PGUID -1 -1 f b t \x2C 0  869 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 651  (  _cidr    PGUID -1 -1 f b t \x2C 0  650 array_in array_out array_in array_out i _null_ ));
#endif
DATA(insert OID = 1042 ( bpchar		 PGUID -1  -1 f b t \x2C 0	18 bpcharin bpcharout bpcharin bpcharout i _null_ ));
DESCR("char(length), blank-padded string, fixed storage length");
#define BPCHAROID		1042
DATA(insert OID = 1043 ( varchar	 PGUID -1  -1 f b t \x2C 0	18 varcharin varcharout varcharin varcharout i _null_ ));
DESCR("varchar(length), non-blank-padded string, variable storage length");
#define VARCHAROID		1043

DATA(insert OID = 1082 ( date		 PGUID	4  10 t b t \x2C 0	0 date_in date_out date_in date_out i _null_ ));
DESCR("ANSI SQL date");
#define DATEOID			1082
DATA(insert OID = 1083 ( time		 PGUID	8  16 f b t \x2C 0	0 time_in time_out time_in time_out d _null_ ));
DESCR("hh:mm:ss, ANSI SQL time");
#define TIMEOID			1083
/* OIDS 1100 - 1199 */
#ifndef NOARRAY
DATA(insert OID = 1182 ( _date		 PGUID	-1 -1 f b t \x2C 0	1082 array_in array_out array_in array_out i _null_ ));
DATA(insert OID = 1183 ( _time		 PGUID	-1 -1 f b t \x2C 0	1083 array_in array_out array_in array_out d _null_ ));
#endif
DATA(insert OID = 1184 ( timestamp	 PGUID	8  47 f b t \x2C 0	0 timestamp_in timestamp_out timestamp_in timestamp_out d _null_ ));
DESCR("date and time");
#define TIMESTAMPOID	1184
#ifndef NOARRAY
DATA(insert OID = 1185 ( _timestamp  PGUID	-1 -1 f b t \x2C 0	1184 array_in array_out array_in array_out d _null_ ));
#endif
DATA(insert OID = 1186 ( interval	 PGUID 12  47 f b t \x2C 0	0 interval_in interval_out interval_in interval_out d _null_ ));
DESCR("@ <number> <units>, time interval");
#define INTERVALOID		1186
#ifndef NOARRAY
DATA(insert OID = 1187 ( _interval	 PGUID	-1 -1 f b t \x2C 0	1186 array_in array_out array_in array_out d _null_ ));
#endif
/* OIDS 1200 - 1299 */
#ifndef NOARRAY
DATA(insert OID = 1231 (  _numeric	 PGUID -1  -1 f b t \x2C 0	1700 array_in array_out array_in array_out i _null_ ));
#endif
DATA(insert OID = 1266 ( timetz		 PGUID 12  22 f b t \x2C 0	0 timetz_in timetz_out timetz_in timetz_out d _null_ ));
DESCR("hh:mm:ss, ANSI SQL time");
#define TIMETZOID		1266
#ifndef NOARRAY
DATA(insert OID = 1270 ( _timetz	 PGUID	-1 -1 f b t \x2C 0	1266 array_in array_out array_in array_out d _null_ ));
#endif
/* OIDS 1500 - 1599 */
DATA(insert OID = 1560 ( bit		 PGUID -1  -1 f b t \x2C 0	0 zpbit_in zpbit_out zpbit_in zpbit_out i _null_ ));
DESCR("fixed-length bit string");
#define ZPBITOID	 1560
#ifndef NOARRAY
DATA(insert OID = 1561 ( _bit		 PGUID	-1 -1 f b t \x2C 0	1560 array_in array_out array_in array_out i _null_ ));
#endif
DATA(insert OID = 1562 ( varbit		 PGUID -1  -1 f b t \x2C 0	0 varbit_in varbit_out varbit_in varbit_out i _null_ ));
DESCR("fixed-length bit string");
#define VARBITOID	  1562
#ifndef NOARRAY
DATA(insert OID = 1563 ( _varbit	 PGUID	-1 -1 f b t \x2C 0	1562 array_in array_out array_in array_out i _null_ ));
#endif
/* OIDS 1600 - 1699 */
DATA(insert OID = 1625 ( lztext		 PGUID -1  -1 f b t \x2C 0	0 lztextin lztextout lztextin lztextout i _null_ ));
DESCR("variable-length string, stored compressed");
#define LZTEXTOID	  1625

/* OIDS 1700 - 1799 */
DATA(insert OID = 1700 ( numeric	   PGUID -1  -1 f b t \x2C 0  0 numeric_in numeric_out numeric_in numeric_out i _null_ ));
DESCR("numeric(precision, decimal), arbitrary precision number");
#define NUMERICOID		1700

#define VARLENA_FIXED_SIZE(attr)	((attr)->atttypid == BPCHAROID && (attr)->atttypmod > 0)

 /*
 * prototypes for functions in pg_type.c
 */
extern Oid	TypeGet(char *typeName, bool *defined);
extern Oid	TypeShellMake(char *typeName);
extern Oid TypeCreate(char *typeName,
		   Oid relationOid,
		   int16 internalSize,
		   int16 externalSize,
		   char typeType,
		   char typDelim,
		   char *inputProcedure,
		   char *outputProcedure,
		   char *receiveProcedure,
		   char *sendProcedure,
		   char *elementTypeName,
		   char *defaultTypeValue,
		   bool passedByValue, char alignment);
extern void TypeRename(const char *oldTypeName, const char *newTypeName);
extern char *makeArrayTypeName(char *typeName);


#endif	 /* PG_TYPE_H */
