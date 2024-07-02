/*-------------------------------------------------------------------------
 *
 * bootstrap.c
 *	  routines to support running postgres in 'bootstrap' mode
 *	bootstrap mode is used to create the initial template database
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <setjmp.h>


#define BOOTSTRAP_INCLUDE		/* mask out stuff in tcop/tcopprot.h */
#include "postgres.h"
#include "env/env.h"
#include "env/dbwriter.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#include "access/genam.h"
#include "access/heapam.h"
#include "bootstrap/bootstrap.h"
#include "catalog/catname.h"
#include "catalog/index.h"
#include "catalog/pg_type.h"
#include "libpq/pqsignal.h"
#include "miscadmin.h"
#include "tcop/tcopprot.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/portal.h"
#include "storage/fd.h" 

#define ALLOC(t, c)		(t *)calloc((unsigned)(c), sizeof(t))

extern void BaseInit(void);
extern void StartupXLOG(void);
extern void ShutdownXLOG(void);
extern void BootStrapXLOG(void);

extern char XLogDir[];
extern char ControlFilePath[];

extern int	Int_yyparse(void);
static hashnode *AddStr(char *str, int strlength, int mderef);
static Form_pg_attribute AllocateAttribute(void);
static int	CompHash(char *str, int len);
static hashnode *FindStr(char *str, int length, hashnode *mderef);
static Oid	gettype(char *type);
static void cleanup(void);


static bool IsUnderPostmaster = false;
/* ----------------
 *		global variables
 * ----------------
 */
/*
 * In the lexical analyzer, we need to get the reference number quickly from
 * the string, and the string from the reference number.  Thus we have
 * as our data structure a hash table, where the hashing key taken from
 * the particular string.  The hash table is chained.  One of the fields
 * of the hash table node is an index into the array of character pointers.
 * The unique index number that every string is assigned is simply the
 * position of its string pointer in the array of string pointers.
 */

#define STRTABLESIZE	10000
#define HASHTABLESIZE	503

/* Hash function numbers */
#define NUM		23
#define NUMSQR	529
#define NUMCUBE 12167

char	   *strtable[STRTABLESIZE];
hashnode   *hashtable[HASHTABLESIZE];

static int	strtable_end = -1;	/* Tells us last occupied string space */

/*-
 * Basic information associated with each type.  This is used before
 * pg_type is created.
 *
 *		XXX several of these input/output functions do catalog scans
 *			(e.g., F_REGPROCIN scans pg_proc).	this obviously creates some
 *			order dependencies in the catalog creation process.
 */
struct typinfo
{
    char		name[NAMEDATALEN];
	Oid			oid;
	Oid			elem;
    int16       len;
    bool		byval;
    char		align;
	Oid			inproc;
	Oid			outproc;
};

static struct typinfo Procid[] = {
	{"bool", BOOLOID, 0, 1,TRUE,'c', F_BOOLIN, F_BOOLOUT},
	{"bytea", BYTEAOID, 0, -1, FALSE,'i', F_BYTEAIN, F_BYTEAOUT},
	{"char", CHAROID, 0, 1, TRUE, 'c', F_CHARIN, F_CHAROUT},
	{"name", NAMEOID, 0, NAMEDATALEN, FALSE, 'i', F_NAMEIN, F_NAMEOUT},
	{"int2", INT2OID, 0, 2, TRUE, 's', F_INT2IN, F_INT2OUT},
	{"int2vector", INT2VECTOROID, 0, INDEX_MAX_KEYS * 2, FALSE, 'i', F_INT2VECTORIN, F_INT2VECTOROUT},
	{"int4", INT4OID, 0, 4, TRUE, 'i', F_INT4IN, F_INT4OUT},
	{"float4", FLOAT4OID, 0, 4, FALSE, 'i', F_FLOAT4IN, F_FLOAT4OUT},
	{"long", LONGOID, 0, LONGSIZE, TRUE, 'l', F_LONGIN, F_LONGOUT},
	{"regproc", REGPROCOID, 0, OIDSIZE, TRUE, 'l', F_REGPROCIN, F_REGPROCOUT},
	{"text", TEXTOID, 0, -1, FALSE, 'i', F_TEXTIN, F_TEXTOUT},
	{"oid", OIDOID, 0, OIDSIZE, TRUE, 'l', F_OIDIN, F_OIDOUT},
	{"tid", TIDOID, 0, TIDSIZE, FALSE, 'i', F_TIDIN, F_TIDOUT},
	{"xid", XIDOID, 0, 8, FALSE, 'd', F_XIDIN, F_XIDOUT},
	{"cid", CIDOID, 0, 4, TRUE, 'i', F_CIDIN, F_CIDOUT},
	{"oidvector", 30, 0, INDEX_MAX_KEYS * OIDSIZE, FALSE, 'l', F_OIDVECTORIN, F_OIDVECTOROUT},
	{"smgr", 210, 0, 2, TRUE, 's', F_SMGRIN, F_SMGROUT}
};

static int	n_types = sizeof(Procid) / sizeof(struct typinfo);

struct typmap
{								/* a hack */
	Oid			am_oid;
	FormData_pg_type am_typ;
};

static struct typmap **Typ = (struct typmap **) NULL;
static struct typmap *Ap = (struct typmap *) NULL;

static int	Warnings = 0;
static char Blanks[MAXATTR];

static char *relname;			/* current relation name */

Form_pg_attribute attrtypes[MAXATTR];	/* points to attribute info */
static char *values[MAXATTR];	/* cooresponding attribute values */
int			numattr;			/* number of attributes for cur. rel */

int			DebugMode;

extern int	optind;
extern char *optarg;

typedef void (*sig_func) ();


/* ----------------------------------------------------------------
 *						misc functions
 * ----------------------------------------------------------------
 */

/* ----------------
 *		error handling / abort routines
 * ----------------
 */
void
err_out(void)
{
	Warnings++;
	cleanup();
}

/* usage:
   usage help for the bootstrap backen
*/
static void
usage(void)
{
	fprintf(stderr, "Usage: postgres -boot [-d] [-C] [-F] [-O] [-Q] [-W]");
	fprintf(stderr, "[-P portno] [dbName]\n");
	fprintf(stderr, "     d: debug mode\n");
	fprintf(stderr, "     C: disable version checking\n");
	fprintf(stderr, "     F: turn off fsync\n");
	fprintf(stderr, "     O: set BootstrapProcessing mode\n");
	fprintf(stderr, "     P portno: specify port number\n");
	fprintf(stderr, "     W: wait for 10 sec. to attach debugger\n");
	proc_exit(1);
}

static void* GetStruct(HeapTuple tuple) {

    void* id = GETSTRUCT(tuple);
    return id;
}

int
BootstrapMain(int argc, char *argv[])
/* ----------------------------------------------------------------
 *	 The main loop for handling the backend in bootstrap mode
 *	 the bootstrap mode is used to initialize the template database
 *	 the bootstrap backend doesn't speak SQL, but instead expects
 *	 commands in a special bootstrap language.
 *
 *	 The arguments passed in to BootstrapMain are the run-time arguments
 *	 without the argument '-boot', the caller is required to have
 *	 removed -boot from the run-time args
 * ----------------------------------------------------------------
 */
{
        bool                wait = false;
	int			i;
	char	   *dbName;
	int			flag;
	bool		xloginit = false;
        Env*            env;
	extern int	optind;
	extern char *optarg;
      
	env = InitSystem(true);   /*    always private */
	
	/* --------------------
	 *	initialize globals
	 * -------------------
	 */
	MyProcPid = getpid();

	/* ----------------
	 *	process command arguments
	 * ----------------
	 */

	/* Set defaults, to be overriden by explicit options below */
	Quiet = false;
	Noversion = false;
	dbName = NULL;
	DataDir = getenv("PGDATA"); /* Null if no PGDATA variable */
	IsUnderPostmaster = false;

	while ((flag = getopt(argc, argv, "D:dCQxpB:FW")) != EOF)
	{
		switch (flag)
		{
			case 'D':
				DataDir = optarg;
				break;
			case 'd':
				DebugMode = true;		/* print out debugging info while
										 * parsing */
				break;
			case 'C':
				Noversion = true;
				break;
			case 'F':
				disableFsync = true;
				break;
			case 'Q':
				Quiet = true;
				break;
			case 'x':
				xloginit = true;
				break;
			case 'p':
				IsUnderPostmaster = true;
				break;
			case 'B':
				NBuffers = atoi(optarg);
				break;
			case 'W':
				wait = true;
				break;
			default:
				usage();
				break;
		}
	}						/* while */

        if ( wait ) sleep(10);

	if (argc - optind > 1)
		usage();
	else if (argc - optind == 1)
		dbName = argv[optind];

	SetProcessingMode(BootstrapProcessing);
	IgnoreSystemIndexes(true);

	if (!DataDir)
	{
		fprintf(stderr, "%s does not know where to find the database system "
				"data.  You must specify the directory that contains the "
				"database system either by specifying the -D invocation "
			 "option or by setting the PGDATA environment variable.\n\n",
				argv[0]);
		proc_exit(1);
	}

	if (dbName == NULL)
	{
		dbName = getenv("USER");
		if (dbName == NULL)
		{
			fputs("bootstrap backend: failed, no db name specified\n", stderr);
			fputs("          and no USER enviroment variable\n", stderr);
			proc_exit(1);
		}
	}
	

	BaseInit();
        InitializeElog(NULL, DebugMode, false);
	SetDatabaseName(dbName);

	if (!IsUnderPostmaster)
	{
		pqsignal(SIGINT, (sig_func) die);
		pqsignal(SIGHUP, (sig_func) die);
		pqsignal(SIGTERM, (sig_func) die);
	}

	/*
	 * Bootstrap under Postmaster means two things: (xloginit) ?
	 * StartupXLOG : ShutdownXLOG
	 *
	 * If !under Postmaster and xloginit then BootStrapXLOG.
	 */
	if (IsUnderPostmaster || xloginit)
	{
		snprintf(XLogDir, MAXPGPATH, "%s%cpg_xlog",
				 DataDir, SEP_CHAR);
		snprintf(ControlFilePath, MAXPGPATH, "%s%cpg_control",
				 DataDir, SEP_CHAR);
	}

	if (IsUnderPostmaster && xloginit)
	{
		StartupXLOG();
		proc_exit(0);
	}

	if (!IsUnderPostmaster && xloginit)
		BootStrapXLOG();

	/*
	 * backend initialization
	 */
	InitPostgres(dbName);
	LockDisable(true);

	if (IsUnderPostmaster && !xloginit)
	{
		ShutdownXLOG();
		proc_exit(0);
	}

	for (i = 0; i < MAXATTR; i++)
	{
		attrtypes[i] = (Form_pg_attribute) NULL;
		Blanks[i] = ' ';
	}
	for (i = 0; i < STRTABLESIZE; ++i)
		strtable[i] = NULL;
	for (i = 0; i < HASHTABLESIZE; ++i)
		hashtable[i] = NULL;

	/* ----------------
	 *	abort processing resumes here
	 * ----------------
	 */
	pqsignal(SIGHUP, handle_warn);

	if (sigsetjmp(Warn_restart, 1) != 0)
	{
		Warnings++;
		SetAbortOnly();
	} else {

	/* ----------------
	 *	process input.
	 * ----------------
	 */

	/*
	 * the sed script boot.sed renamed yyparse to Int_yyparse for the
	 * bootstrap parser to avoid conflicts with the normal SQL parser
	 */
        Int_yyparse();
    }

	/* clean up processing */
/*	StartTransactionCommand();*/
	cleanup();

	
	/* not reached, here to make compiler happy */
	return 0;

}

/* ----------------------------------------------------------------
 *				MANUAL BACKEND INTERACTIVE INTERFACE COMMANDS
 * ----------------------------------------------------------------
 */

/* ----------------
 *		boot_openrel
 * ----------------
 */
void
boot_openrel(char *relname)
{
	int			i;
	struct typmap **app;
	Relation	rel;
	HeapScanDesc scan;
	HeapTuple	tup;

	if (strlen(relname) >= NAMEDATALEN - 1)
		relname[NAMEDATALEN - 1] = '\0';

	if (Typ == (struct typmap **) NULL)
	{
		rel = heap_openr(TypeRelationName, NoLock);
		Assert(rel);
		scan = heap_beginscan(rel, SnapshotNow, 0, (ScanKey) NULL);
		i = 0;
		while (HeapTupleIsValid(tup = heap_getnext(scan)))
                    ++i;

		heap_endscan(scan);
		app = Typ = ALLOC(struct typmap *, i + 1);
		while (i-- > 0)
			*app++ = ALLOC(struct typmap, 1);
		*app = (struct typmap *) NULL;
		scan = heap_beginscan(rel, SnapshotNow, 0, (ScanKey) NULL);
		app = Typ;
		while (HeapTupleIsValid(tup = heap_getnext(scan)))
		{
			(*app)->am_oid = tup->t_data->t_oid;
			memmove((char *) &(*app++)->am_typ,
					(char *) GETSTRUCT(tup),
					sizeof((*app)->am_typ));
		}
		heap_endscan(scan);
		heap_close(rel, NoLock);
	}

	if (reldesc != NULL)
		closerel(NULL);

	if (!Quiet)
		printf("Amopen: relation %s. attrsize %d\n", relname ? relname : "(null)",
			   (int) ATTRIBUTE_TUPLE_SIZE);

	reldesc = heap_openr(relname, NoLock);
	Assert(reldesc);
	numattr = reldesc->rd_rel->relnatts;
	for (i = 0; i < numattr; i++)
	{
		if (attrtypes[i] == NULL)
			attrtypes[i] = AllocateAttribute();
		memmove((char *) attrtypes[i],
				(char *) reldesc->rd_att->attrs[i],
				ATTRIBUTE_TUPLE_SIZE);

		/* Some old pg_attribute tuples might not have attisset. */

		/*
		 * If the attname is attisset, don't look for it - it may not be
		 * defined yet.
		 */
		if (namestrcmp(&attrtypes[i]->attname, "attisset") == 0)
			attrtypes[i]->attisset = get_attisset(RelationGetRelid(reldesc),
										 NameStr(attrtypes[i]->attname));
		else
			attrtypes[i]->attisset = false;

		if (DebugMode)
		{
			Form_pg_attribute at = attrtypes[i];

			printf("create attribute %d name %s len %d num %d type %ld align %c\n",
				   i, NameStr(at->attname), at->attlen, at->attnum,
				   at->atttypid,at->attalign
				);
			fflush(stdout);
		}
	}
}

/* ----------------
 *		closerel
 * ----------------
 */
void
closerel(char *name)
{
	if (name)
	{
		if (reldesc)
		{
			if (strcmp(RelationGetRelationName(reldesc), name) != 0)
				elog(ERROR, "closerel: close of '%s' when '%s' was expected",
					 name, relname ? relname : "(null)");
		}
		else
			elog(ERROR, "closerel: close of '%s' before any relation was opened",
				 name);

	}

	if (reldesc == NULL)
		elog(ERROR, "Warning: no opened relation to close.\n");
	else
	{
		if (!Quiet)
			printf("Amclose: relation %s.\n", relname ? relname : "(null)");
		heap_close(reldesc, NoLock);
		reldesc = (Relation) NULL;
	}
}



/* ----------------
 * DEFINEATTR()
 *
 * define a <field,type> pair
 * if there are n fields in a relation to be created, this routine
 * will be called n times
 * ----------------
 */
void
DefineAttr(char *name, char *type, int attnum)
{
	int			attlen;
	Oid			typeoid;

	if (reldesc != NULL)
	{
		fputs("Warning: no open relations allowed with 't' command.\n", stderr);
		closerel(relname);
	}

	typeoid = gettype(type);
	if (attrtypes[attnum] == (Form_pg_attribute) NULL)
		attrtypes[attnum] = AllocateAttribute();
	if (Typ != (struct typmap **) NULL)
	{
		attrtypes[attnum]->atttypid = Ap->am_oid;
		namestrcpy(&attrtypes[attnum]->attname, name);
		if (!Quiet)
			printf("<%s %s> ", NameStr(attrtypes[attnum]->attname), type);
		attrtypes[attnum]->attnum = 1 + attnum; /* fillatt */
		attlen = attrtypes[attnum]->attlen = Ap->am_typ.typlen;
		attrtypes[attnum]->attbyval = Ap->am_typ.typbyval;
		attrtypes[attnum]->attstorage = 'p';
		attrtypes[attnum]->attalign = Ap->am_typ.typalign;
	}
	else
	{
		attrtypes[attnum]->atttypid = Procid[typeoid].oid;
		namestrcpy(&attrtypes[attnum]->attname, name);
		if (!Quiet)
			printf("<%s %s> ", NameStr(attrtypes[attnum]->attname), type);
		attrtypes[attnum]->attnum = 1 + attnum; /* fillatt */
		attlen = attrtypes[attnum]->attlen = Procid[typeoid].len;
		attrtypes[attnum]->attbyval = Procid[typeoid].byval;
		attrtypes[attnum]->attalign = Procid[typeoid].align;
		attrtypes[attnum]->attstorage = 'p';

		/*
		 * Cheat like mad to fill in these items from the length only.
		 * This only has to work for types used in the system catalogs...
		 */
/*
		switch (attlen)
		{
			case 1:
				attrtypes[attnum]->attbyval = true;
				attrtypes[attnum]->attalign = 'c';
				break;
			case 2:
				attrtypes[attnum]->attbyval = true;
				attrtypes[attnum]->attalign = 's';
				break;
			case 4:
				attrtypes[attnum]->attbyval = true;
				attrtypes[attnum]->attalign = 'i';
				break;
			default:
				attrtypes[attnum]->attbyval = false;
				attrtypes[attnum]->attalign = 'i';
				break;
		}
*/
	}

	attrtypes[attnum]->attcacheoff = -1;
	attrtypes[attnum]->atttypmod = -1;
}


/* ----------------
 *		InsertOneTuple
 *		assumes that 'oid' will not be zero.
 * ----------------
 */
void
InsertOneTuple(Oid objectid)
{
	HeapTuple	tuple;
	TupleDesc	tupDesc;

	int			i;

	if (DebugMode)
	{
		printf("InsertOneTuple oid %lu, %d attrs\n", objectid, numattr);
		fflush(stdout);
	}

	tupDesc = CreateTupleDesc(numattr, attrtypes);
	tuple = heap_formtuple(tupDesc, (Datum *) values, Blanks);
        GetStruct(tuple);
	pfree(tupDesc);				/* just free's tupDesc, not the attrtypes */

	if (objectid != (Oid) 0)
		tuple->t_data->t_oid = objectid;
	heap_insert(reldesc, tuple);
	heap_freetuple(tuple);

	if (DebugMode)
	{
		printf("End InsertOneTuple, objectid=%lu\n", objectid);
		fflush(stdout);
	}

	/*
	 * Reset blanks for next tuple
	 */
	for (i = 0; i < numattr; i++)
		Blanks[i] = ' ';
}

/* ----------------
 *		InsertOneValue
 * ----------------
 */
void
InsertOneValue(Oid objectid, char *value, int i)
{
	int			typeindex;
	char	   *prt;
	struct typmap **app;
    
	if ( strcmp("LONGSIZE",value) == 0 ) ltoa(LONGSIZE,value);
	if ( strcmp("OIDSIZE",value) == 0 ) ltoa(OIDSIZE,value);
	if ( strcmp("TIDSIZE",value) == 0 ) ltoa(TIDSIZE,value);
	if ( strcmp("OIDARRAYSIZE",value) == 0 ) ltoa(INDEX_MAX_KEYS*OIDSIZE,value);
    
    if (DebugMode)
		printf("Inserting value: '%s'\n", value);
	if (i < 0 || i >= MAXATTR)
	{
		printf("i out of range: %d\n", i);
		Assert(0);
	}

	if (Typ != (struct typmap **) NULL)
	{
		struct typmap *ap;

		if (DebugMode)
			puts("Typ != NULL");
		app = Typ;
		while (*app && (*app)->am_oid != reldesc->rd_att->attrs[i]->atttypid)
			++app;
		ap = *app;
		if (ap == NULL)
		{
			printf("Unable to find atttypid in Typ list! %lu\n",
				   reldesc->rd_att->attrs[i]->atttypid
				);
			Assert(0);
		}
		values[i] = fmgr(ap->am_typ.typinput,
						 value,
						 ap->am_typ.typelem,
						 -1);
		prt = fmgr(ap->am_typ.typoutput, values[i],
				   ap->am_typ.typelem,
				   -1);
		if (DebugMode)
			printf("out %s ", prt);
		pfree(prt);
	}
	else
	{
		for (typeindex = 0; typeindex < n_types; typeindex++)
		{
			if (Procid[typeindex].oid == attrtypes[i]->atttypid)
				break;
		}
		if (typeindex >= n_types)
			elog(ERROR, "can't find type OID %lu", attrtypes[i]->atttypid);
		if (DebugMode)
			printf("Typ == NULL, typeindex = %u idx = %d\n", typeindex, i);
		values[i] = fmgr(Procid[typeindex].inproc, value,
						 Procid[typeindex].elem, -1);
		prt = fmgr(Procid[typeindex].outproc, values[i],
				   Procid[typeindex].elem);
		if (!Quiet)
			printf("%s ", prt);
		pfree(prt);
	}
	if (DebugMode)
	{
		puts("End InsertValue");
		fflush(stdout);
	}
}

/* ----------------
 *		InsertOneNull
 * ----------------
 */
void
InsertOneNull(int i)
{
	if (DebugMode)
		printf("Inserting null\n");
	if (i < 0 || i >= MAXATTR)
		elog(FATAL, "i out of range (too many attrs): %d\n", i);
	values[i] = (char *) NULL;
	Blanks[i] = 'n';
}

/* ----------------
 *		cleanup
 * ----------------
 */
static void
cleanup()
{
	static int	beenhere = 0;

	if (!beenhere)
		beenhere = 1;
	else
	{
		elog(FATAL, "Memory manager fault: cleanup called twice.\n");
		proc_exit(1);
	}
	if (reldesc != (Relation) NULL)
		heap_close(reldesc, NoLock);

	/*CommitTransactionCommand();*/
	proc_exit(Warnings);
}

/* ----------------
 *		gettype
 * ----------------
 */
static Oid
gettype(char *type)
{
	int			i;
	Relation	rel;
	HeapScanDesc scan;
	HeapTuple	tup;
	struct typmap **app;

	if (Typ != (struct typmap **) NULL)
	{
		for (app = Typ; *app != (struct typmap *) NULL; app++)
		{
			if (strncmp(NameStr((*app)->am_typ.typname), type, NAMEDATALEN) == 0)
			{
				Ap = *app;
				return (*app)->am_oid;
			}
		}
	}
	else
	{
		for (i = 0; i <= n_types; i++)
		{
			if (strncmp(type, Procid[i].name, NAMEDATALEN) == 0)
				return i;
		}
		if (DebugMode)
			printf("bootstrap.c: External Type: %s\n", type);
		rel = heap_openr(TypeRelationName, NoLock);
		Assert(rel);
		scan = heap_beginscan(rel, SnapshotNow, 0, (ScanKey) NULL);
		i = 0;
		while (HeapTupleIsValid(tup = heap_getnext(scan)))
			++i;
		heap_endscan(scan);
		app = Typ = ALLOC(struct typmap *, i + 1);
		while (i-- > 0)
			*app++ = ALLOC(struct typmap, 1);
		*app = (struct typmap *) NULL;
		scan = heap_beginscan(rel, SnapshotNow, 0, (ScanKey) NULL);
		app = Typ;
		while (HeapTupleIsValid(tup = heap_getnext(scan)))
		{
			(*app)->am_oid = tup->t_data->t_oid;
			memmove((char *) &(*app++)->am_typ,
					(char *) GETSTRUCT(tup),
					sizeof((*app)->am_typ));
		}
		heap_endscan(scan);
		heap_close(rel, NoLock);
		return gettype(type);
	}
	elog(ERROR, "Error: unknown type '%s'.\n", type);
	err_out();
	/* not reached, here to make compiler happy */
	return 0;
}

/* ----------------
 *		AllocateAttribute
 * ----------------
 */
static Form_pg_attribute		/* XXX */
AllocateAttribute()
{
	Form_pg_attribute attribute = (Form_pg_attribute) malloc(ATTRIBUTE_TUPLE_SIZE);

	if (!PointerIsValid(attribute))
		elog(FATAL, "AllocateAttribute: malloc failed");
	MemSet(attribute, 0, ATTRIBUTE_TUPLE_SIZE);

	return attribute;
}

/* ----------------
 *		MapArrayTypeName
 * XXX arrays of "basetype" are always "_basetype".
 *	   this is an evil hack inherited from rel. 3.1.
 * XXX array dimension is thrown away because we
 *	   don't support fixed-dimension arrays.  again,
 *	   sickness from 3.1.
 *
 * the string passed in must have a '[' character in it
 *
 * the string returned is a pointer to static storage and should NOT
 * be freed by the CALLER.
 * ----------------
 */
char *
MapArrayTypeName(char *s)
{
	int			i,
				j;
	static char newStr[NAMEDATALEN];	/* array type names < NAMEDATALEN
										 * long */

	if (s == NULL || s[0] == '\0')
		return s;

	j = 1;
	newStr[0] = '_';
	for (i = 0; i < NAMEDATALEN - 1 && s[i] != '['; i++, j++)
		newStr[j] = s[i];

	newStr[j] = '\0';

	return newStr;
}

/* ----------------
 *		EnterString
 *		returns the string table position of the identifier
 *		passed to it.  We add it to the table if we can't find it.
 * ----------------
 */
int
EnterString(char *str)
{
	hashnode   *node;
	int			len;

	len = strlen(str);

	node = FindStr(str, len, 0);
	if (node)
		return node->strnum;
	else
	{
		node = AddStr(str, len, 0);
		return node->strnum;
	}
}

/* ----------------
 *		LexIDStr
 *		when given an idnum into the 'string-table' return the string
 *		associated with the idnum
 * ----------------
 */
char *
LexIDStr(int ident_num)
{
	return strtable[ident_num];
}


/* ----------------
 *		CompHash
 *
 *		Compute a hash function for a given string.  We look at the first,
 *		the last, and the middle character of a string to try to get spread
 *		the strings out.  The function is rather arbitrary, except that we
 *		are mod'ing by a prime number.
 * ----------------
 */
static int
CompHash(char *str, int len)
{
	int			result;

	result = (NUM * str[0] + NUMSQR * str[len - 1] + NUMCUBE * str[(len - 1) / 2]);

	return result % HASHTABLESIZE;

}

/* ----------------
 *		FindStr
 *
 *		This routine looks for the specified string in the hash
 *		table.	It returns a pointer to the hash node found,
 *		or NULL if the string is not in the table.
 * ----------------
 */
static hashnode *
FindStr(char *str, int length, hashnode *mderef)
{
	hashnode   *node;

	node = hashtable[CompHash(str, length)];
	while (node != NULL)
	{

		/*
		 * We must differentiate between string constants that might have
		 * the same value as a identifier and the identifier itself.
		 */
		if (!strcmp(str, strtable[node->strnum]))
		{
			return node;		/* no need to check */
		}
		else
			node = node->next;
	}
	/* Couldn't find it in the list */
	return NULL;
}

/* ----------------
 *		AddStr
 *
 *		This function adds the specified string, along with its associated
 *		data, to the hash table and the string table.  We return the node
 *		so that the calling routine can find out the unique id that AddStr
 *		has assigned to this string.
 * ----------------
 */
static hashnode *
AddStr(char *str, int strlength, int mderef)
{
	hashnode   *temp,
			   *trail,
			   *newnode;
	int			hashresult;
	int			len;

	if (++strtable_end == STRTABLESIZE)
	{
		/* Error, string table overflow, so we Punt */
		elog(FATAL,
			 "There are too many string constants and identifiers for the compiler to handle.");


	}

	/*
	 * Some of the utilites (eg, define type, create relation) assume that
	 * the string they're passed is a NAMEDATALEN.  We get array bound
	 * read violations from purify if we don't allocate at least
	 * NAMEDATALEN bytes for strings of this sort.	Because we're lazy, we
	 * allocate at least NAMEDATALEN bytes all the time.
	 */

	if ((len = strlength + 1) < NAMEDATALEN)
		len = NAMEDATALEN;

	strtable[strtable_end] = malloc((unsigned) len);
	strcpy(strtable[strtable_end], str);

	/* Now put a node in the hash table */

	newnode = (hashnode *) malloc(sizeof(hashnode) * 1);
	newnode->strnum = strtable_end;
	newnode->next = NULL;

	/* Find out where it goes */

	hashresult = CompHash(str, strlength);
	if (hashtable[hashresult] == NULL)
		hashtable[hashresult] = newnode;
	else
	{							/* There is something in the list */
		trail = hashtable[hashresult];
		temp = trail->next;
		while (temp != NULL)
		{
			trail = temp;
			temp = temp->next;
		}
		trail->next = newnode;
	}
	return newnode;
}
