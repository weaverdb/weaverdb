/*-------------------------------------------------------------------------
 *
 * fmgr.c
 *	  Interface routines for the table-driven function manager.
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

#include <strings.h>

#include "nodes/pg_list.h"
#include "catalog/pg_language.h"
#include "catalog/pg_proc.h"
#include "commands/trigger.h"
#include "utils/builtins.h"
#include "utils/fmgrtab.h"
#include "utils/syscache.h"
#include "utils/java.h"
#include "utils/relcache.h"
#ifdef UNUSED
/*
 * Interface for PL functions
 *
 * XXX: use of global fmgr_pl_finfo variable is really ugly.  FIXME
 */
/*
FmgrInfo        *fmgr_pl_finfo;
*/
static char *
fmgr_pl(char *arg0,...)
{
    elog(ERROR,"procedural language functions not implemented");
	return NULL;
}

/*
 * Interface for untrusted functions
 */

static char *
fmgr_untrusted(char *arg0,...)
{

	/*
	 * Currently these are unsupported.  Someday we might do something
	 * like forking a subprocess to execute 'em.
	 */
	elog(ERROR, "Untrusted functions not supported.");
	return NULL;				/* keep compiler happy */
}
#endif


/*
 * Interface for SQL-language functions
 */

static char *
fmgr_sql(char *arg0,...)
{

	/*
	 * XXX It'd be really nice to support SQL functions anywhere that
	 * builtins are supported.	What would we have to do?  What pitfalls
	 * are there?
	 */
	elog(ERROR, "SQL-language function not supported in this context.");
	return NULL;				/* keep compiler happy */
}

/*
 * fmgr_c is not really for C functions only; it can be called for functions
 * in any language.  Many parts of the system use this entry point if they
 * want to pass the arguments in an array rather than as explicit arguments.
 */

char *
fmgr_c(FmgrInfo *finfo,
	   FmgrValues *values,
	   bool *isNull)
{
	char	   *returnValue = (char *) NULL;

	/*
	 * If finfo contains a PL handler for this function, call that
	 * instead.
	 */

	if (finfo->fn_addr == NULL && finfo->fn_data == NULL)
		elog(ERROR, "Internal error: fmgr_c received NULL function pointer.");

	switch (finfo->fn_nargs)
	{
		case 0:
			returnValue = (*fmgr_faddr(finfo)) ();
			break;
		case 1:
			/* NullValue() uses isNull to check if args[0] is NULL */
			returnValue = (*fmgr_faddr_1(finfo)) (values->data[0]);
			break;
		case 2:
			returnValue = (*fmgr_faddr_2(finfo)) (values->data[0], values->data[1]);
			break;
		case 3:
			returnValue = (*fmgr_faddr_3(finfo)) (values->data[0], values->data[1],
									  values->data[2]);
			break;
		case 4:
			returnValue = (*fmgr_faddr_4(finfo)) (values->data[0], values->data[1],
									  values->data[2], values->data[3]);
			break;
		case 5:
			returnValue = (*fmgr_faddr_5(finfo)) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4]);
			break;
		case 6:
			returnValue = (*fmgr_faddr_6(finfo)) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5]);
			break;
		case 7:
			returnValue = (*fmgr_faddr_7(finfo)) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6]);
			break;
		case 8:
			returnValue = (*fmgr_faddr_8(finfo)) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7]);
			break;
		case 9:
			returnValue = (*fmgr_faddr_9(finfo)) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8]);
			break;
#if FUNC_MAX_ARGS >= 10
		case 10:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9]);
			break;
#endif
#if FUNC_MAX_ARGS >= 11
		case 11:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10]);
			break;
#endif
#if FUNC_MAX_ARGS >= 12
		case 12:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11]);
			break;
#endif
#if FUNC_MAX_ARGS >= 13
		case 13:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12]);
			break;
#endif
#if FUNC_MAX_ARGS >= 14
		case 14:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13]);
			break;
#endif
#if FUNC_MAX_ARGS >= 15
		case 15:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14]);
			break;
#endif
#if FUNC_MAX_ARGS >= 16
		case 16:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15]);
			break;
#endif
#if FUNC_MAX_ARGS >= 17
		case 17:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15],
									  values->data[16]);
			break;
#endif
#if FUNC_MAX_ARGS >= 18
		case 18:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15],
									  values->data[16], values->data[17]);
			break;
#endif
#if FUNC_MAX_ARGS >= 19
		case 19:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15],
									  values->data[16], values->data[17],
									  values->data[18]);
			break;
#endif
#if FUNC_MAX_ARGS >= 20
		case 20:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15],
									  values->data[16], values->data[17],
									  values->data[18], values->data[19]);
			break;
#endif
#if FUNC_MAX_ARGS >= 21
		case 21:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15],
									  values->data[16], values->data[17],
									  values->data[18], values->data[19],
									  values->data[20]);
			break;
#endif
#if FUNC_MAX_ARGS >= 22
		case 22:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15],
									  values->data[16], values->data[17],
									  values->data[18], values->data[19],
									  values->data[20], values->data[21]);
			break;
#endif
#if FUNC_MAX_ARGS >= 23
		case 23:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15],
									  values->data[16], values->data[17],
									  values->data[18], values->data[19],
									  values->data[20], values->data[21],
									  values->data[22]);
			break;
#endif
#if FUNC_MAX_ARGS >= 24
		case 24:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15],
									  values->data[16], values->data[17],
									  values->data[18], values->data[19],
									  values->data[20], values->data[21],
									  values->data[22], values->data[23]);
			break;
#endif
#if FUNC_MAX_ARGS >= 25
		case 25:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15],
									  values->data[16], values->data[17],
									  values->data[18], values->data[19],
									  values->data[20], values->data[21],
									  values->data[22], values->data[23],
									  values->data[24]);
			break;
#endif
#if FUNC_MAX_ARGS >= 26
		case 26:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15],
									  values->data[16], values->data[17],
									  values->data[18], values->data[19],
									  values->data[20], values->data[21],
									  values->data[22], values->data[23],
									  values->data[24], values->data[25]);
			break;
#endif
#if FUNC_MAX_ARGS >= 27
		case 27:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15],
									  values->data[16], values->data[17],
									  values->data[18], values->data[19],
									  values->data[20], values->data[21],
									  values->data[22], values->data[23],
									  values->data[24], values->data[25],
									  values->data[26]);
			break;
#endif
#if FUNC_MAX_ARGS >= 28
		case 28:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15],
									  values->data[16], values->data[17],
									  values->data[18], values->data[19],
									  values->data[20], values->data[21],
									  values->data[22], values->data[23],
									  values->data[24], values->data[25],
									  values->data[26], values->data[27]);
			break;
#endif
#if FUNC_MAX_ARGS >= 29
		case 29:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15],
									  values->data[16], values->data[17],
									  values->data[18], values->data[19],
									  values->data[20], values->data[21],
									  values->data[22], values->data[23],
									  values->data[24], values->data[25],
									  values->data[26], values->data[27],
									  values->data[28]);
			break;
#endif
#if FUNC_MAX_ARGS >= 30
		case 30:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15],
									  values->data[16], values->data[17],
									  values->data[18], values->data[19],
									  values->data[20], values->data[21],
									  values->data[22], values->data[23],
									  values->data[24], values->data[25],
									  values->data[26], values->data[27],
									  values->data[28], values->data[29]);
			break;
#endif
#if FUNC_MAX_ARGS >= 31
		case 31:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15],
									  values->data[16], values->data[17],
									  values->data[18], values->data[19],
									  values->data[20], values->data[21],
									  values->data[22], values->data[23],
									  values->data[24], values->data[25],
									  values->data[26], values->data[27],
									  values->data[28], values->data[29],
									  values->data[30]);
			break;
#endif
#if FUNC_MAX_ARGS >= 32
		case 32:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8], values->data[9],
									  values->data[10], values->data[11],
									  values->data[12], values->data[13],
									  values->data[14], values->data[15],
									  values->data[16], values->data[17],
									  values->data[18], values->data[19],
									  values->data[20], values->data[21],
									  values->data[22], values->data[23],
									  values->data[24], values->data[25],
									  values->data[26], values->data[27],
									  values->data[28], values->data[29],
									  values->data[30], values->data[31]);
			break;
#endif
		default:
			elog(ERROR, "fmgr_c: function %lu: too many arguments (%d > %d)",
				 finfo->fn_oid, finfo->fn_nargs, FUNC_MAX_ARGS);
			break;
	}
	return returnValue;
}

/*
 * Expand a regproc OID into an FmgrInfo cache struct.
 */

Oid
fmgr_info(Oid procedureId, FmgrInfo *finfo)
{
	FmgrCall   *fcp;
	HeapTuple	procedureTuple;
	FormData_pg_proc *procedureStruct;
	Oid			language;
	char	   *prosrc;

	finfo->fn_addr = NULL;
	finfo->fn_oid = procedureId;

	if ((fcp = fmgr_isbuiltin(procedureId)) != NULL)
	{

		/*
		 * Fast path for builtin functions: don't bother consulting
		 * pg_proc
		 */
		finfo->fn_addr = fcp->func;
		finfo->fn_nargs = fcp->nargs;
                language = INTERNALlanguageId;
	}
	else
	{
		procedureTuple = SearchSysCacheTuple(PROCOID,
                                                       ObjectIdGetDatum(procedureId),
                                                             0, 0, 0);
		if (!HeapTupleIsValid(procedureTuple))
		{
			elog(ERROR, "fmgr_info: function %lu: cache lookup failed",
				 procedureId);
		}
		procedureStruct = (FormData_pg_proc *) GETSTRUCT(procedureTuple);

		language = procedureStruct->prolang;
		switch (language)
		{
			case INTERNALlanguageId:

				/*
				 * For an ordinary builtin function, we should never get
				 * here because the isbuiltin() search above will have
				 * succeeded. However, if the user has done a CREATE
				 * FUNCTION to create an alias for a builtin function, we
				 * end up here.  In that case we have to look up the
				 * function by name.  The name of the internal function is
				 * stored in prosrc (it doesn't have to be the same as the
				 * name of the alias!)
				 */
				prosrc = textout((text*)SysCacheGetAttr(PROCOID,procedureTuple,Anum_pg_proc_prosrc,NULL));
				finfo->fn_addr = fmgr_lookupByName(prosrc);
				if (!finfo->fn_addr)
					elog(ERROR, "fmgr_info: function %s not in internal table",
						 prosrc);
				finfo->fn_nargs = procedureStruct->pronargs;
				pfree(prosrc);
				break;
			case ClanguageId:
				/*  removing dynamic load for now b/c it doesn't work with
					java vm  MKS 11.23.2001  
				
				finfo->fn_addr = fmgr_dynamic(procedureId, &(finfo->fn_nargs));
				*/
				break;
			case SQLlanguageId:
				finfo->fn_addr = (func_ptr) fmgr_sql;
				finfo->fn_nargs = procedureStruct->pronargs;
				break;
			case JAVAlanguageId:
				finfo->fn_addr = (func_ptr) NULL;
				finfo->fn_nargs = procedureStruct->pronargs;
                                {
                                    finfo->fn_data = GetJavaFunction(NameStr(procedureStruct->proname), procedureStruct->pronargs, 
                                            procedureStruct->proargtypes);
                                }
				break;
			default:
					elog(ERROR, "fmgr_info: %s %lu",
						 "Cache lookup for language failed",
						 DatumGetObjectId(procedureStruct->prolang));
				break;
		}
	}
        return language;
}

/*
 *		fmgr			- return the value of a function call
 *
 *		If the function is a system routine, it's compiled in, so call
 *		it directly.
 *
 *		Otherwise pass it to the the appropriate 'language' function caller.
 *
 *		Returns the return value of the invoked function if succesful,
 *		0 if unsuccessful.
 */
char *
fmgr(Oid procedureId,...)
{
	va_list		pvar;
	int			i;
	int			pronargs;
	FmgrInfo	finfo;
	bool		isNull = false;
        Oid             language;

	language = fmgr_info(procedureId, &finfo);
	pronargs = finfo.fn_nargs;

	if (pronargs > FUNC_MAX_ARGS)
		elog(ERROR, "fmgr: function %lu: too many arguments (%d > %d)",
			 procedureId, pronargs, FUNC_MAX_ARGS);

        if ( language == JAVAlanguageId ) {
            Datum      values[FUNC_MAX_ARGS];
            Oid      returnType;
            
            va_start(pvar, procedureId);

            for (i = 0; i < pronargs; ++i)
                    values[i] = va_arg(pvar, Datum);

            va_end(pvar);
            return (char*)fmgr_cached_javaA(finfo.fn_data,finfo.fn_nargs,values, &returnType, &isNull);
        } else {
            FmgrValues	values;
            va_start(pvar, procedureId);

            for (i = 0; i < pronargs; ++i)
                    values.data[i] = va_arg(pvar, Datum);

            va_end(pvar);
            return fmgr_c(&finfo, &values, &isNull);
        }
}

/*
 * This is just a version of fmgr() in which the hacker can prepend a C
 * function pointer.  This routine is not normally called; generally,
 * if you have all of this information you're likely to just jump through
 * the pointer, but it's available for use with macros in fmgr.h if you
 * want this routine to do sanity-checking for you.
 *
 * funcinfo, n_arguments, args...
 */
#ifdef TRACE_FMGR_PTR

char *
fmgr_ptr(FmgrInfo *finfo, ...)
{
	va_list		pvar;
	int			i;
	FmgrValues	values;
	bool		isNull = false;

	va_start(pvar, finfo);
	if (finfo->fn_nargs > FUNC_MAX_ARGS)
	{
		elog(ERROR, "fmgr_ptr: function %lu: too many arguments (%d > %d)",
			 finfo->fn_oid, finfo->fn_nargs, FUNC_MAX_ARGS);
	}
	for (i = 0; i < finfo->fn_nargs; ++i)
		values.data[i] = va_arg(pvar, Datum);
	va_end(pvar);

	/* XXX see WAY_COOL_ORTHOGONAL_FUNCTIONS */
	return fmgr_c(finfo, &values, &isNull);
}

#endif

/*
 * This routine is not well thought out.  When I get around to adding a
 * function pointer field to FuncIndexInfo, it will be replace by calls
 * to fmgr_c().
 */
char *
fmgr_array_args(Oid procedureId, int nargs, char *args[], bool *isNull)
{
	FmgrInfo	finfo;

	fmgr_info(procedureId, &finfo);
	finfo.fn_nargs = nargs;

        if (finfo.fn_addr == NULL) {
            return (char*)fmgr_cached_javaA(finfo.fn_data, nargs, ((FmgrValues *) args)->data, NULL, isNull);
        } else {
	/* XXX see WAY_COOL_ORTHOGONAL_FUNCTIONS */
            return fmgr_c(&finfo,
				  (FmgrValues *) args,
				  isNull);
        }
}
