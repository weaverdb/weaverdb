/*-------------------------------------------------------------------------
 *
 * selfuncs.c
 *	  Selectivity functions and index cost estimation functions for
 *	  standard operators and index access methods.
 *
 *	  Selectivity routines are registered in the pg_operator catalog
 *	  in the "oprrest" and "oprjoin" attributes.
 *
 *	  Index cost functions are registered in the pg_am catalog
 *	  in the "amcostestimate" attribute.
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
#include <ctype.h>
#include <math.h>


#include "c.h"
#include "postgres.h"

#include "access/attnum.h"
#include "access/heapam.h"
#include "catalog/catname.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_statistic.h"
#include "catalog/pg_type.h"
#include "mb/pg_wchar.h"
#include "optimizer/cost.h"
#include "parser/parse_func.h"
#include "parser/parse_oper.h"
#include "utils/builtins.h"
#include "utils/int8.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "utils/wrapdatum.h"
#include "utils/java.h"
#include "commands/variable.h"

/* N is not a valid var/constant or relation id */
#define NONVALUE(N)		((N) == 0)

/* are we looking at a functional index selectivity request? */
#define FunctionalSelectivity(nIndKeys,attNum) ((attNum)==InvalidAttrNumber)

/* default selectivity estimate for equalities such as "A = b" */
#define DEFAULT_EQ_SEL	0.01

/* default selectivity estimate for inequalities such as "A < b" */
#define DEFAULT_INEQ_SEL  (1.0 / 3.0)

/* default selectivity estimate for pattern-match operators such as LIKE */
#define DEFAULT_MATCH_SEL	0.01

/* "fudge factor" for estimating frequency of not-most-common values */
#define NOT_MOST_COMMON_RATIO  0.1

static bool convert_to_scalar(Datum value, Oid valuetypid, double *scaledvalue,
							  Datum lobound, Datum hibound, Oid boundstypid,
							  double *scaledlobound, double *scaledhibound);
static double convert_numeric_to_scalar(Datum value, Oid typid);
static void convert_string_to_scalar(unsigned char *value,
									 double *scaledvalue,
									 unsigned char *lobound,
									 double *scaledlobound,
									 unsigned char *hibound,
									 double *scaledhibound);
static double convert_one_string_to_scalar(unsigned char *value,
										   int rangelo, int rangehi);
static unsigned char * convert_string_datum(Datum value, Oid typid);
static double convert_timevalue_to_scalar(Datum value, Oid typid);
static void getattproperties(Oid relid, AttrNumber attnum,
				 Oid *typid,
				 int *typlen,
				 bool *typbyval,
				 int32 *typmod);
static bool getattstatistics(Oid relid, AttrNumber attnum,
				 Oid typid, int32 typmod,
				 double *nullfrac,
				 double *commonfrac,
				 wrapped_datum **commonval,
				 wrapped_datum **loval,
				 wrapped_datum **hival);
static Selectivity prefix_selectivity(char *prefix,
									  Oid relid,
									  AttrNumber attno,
									  Oid datatype);
static Selectivity pattern_selectivity(char *patt, Pattern_Type ptype);
static bool string_lessthan(const char *str1, const char *str2,
				Oid datatype);
static Oid	find_operator(const char *opname, Oid datatype);
static Datum string_to_datum(const char *str, Oid datatype);


/*
 *		eqsel			- Selectivity of "=" for any data types.
 *
 * Note: this routine is also used to estimate selectivity for some
 * operators that are not "=" but have comparable selectivity behavior,
 * such as "~=" (geometric approximate-match).  Even for "=", we must
 * keep in mind that the left and right datatypes may differ, so the type
 * of the given constant "value" may be different from the type of the
 * attribute.
 */
float64
eqsel(Oid opid,
	  Oid relid,
	  AttrNumber attno,
	  Datum value,
	  int32 flag)
{
	float64		result;

	result = (float64) palloc(sizeof(float64data));
	if (NONVALUE(attno) || NONVALUE(relid))
		*result = DEFAULT_EQ_SEL;
	else
	{
		Oid			typid;
		int			typlen;
		bool		typbyval;
		int32		typmod;
		double		nullfrac;
		double		commonfrac;
		wrapped_datum*		commonval;
		double		selec;

		/* get info about the attribute */
		getattproperties(relid, attno,
						 &typid, &typlen, &typbyval, &typmod);

		/* get stats for the attribute, if available */
		if (getattstatistics(relid, attno, typid, typmod,
							 &nullfrac, &commonfrac, &commonval,
							 NULL, NULL))
		{
			if (flag & SEL_CONSTANT)
			{

				/*
				 * Is the constant "=" to the column's most common value?
				 * (Although the operator may not really be "=", we will
				 * assume that seeing whether it returns TRUE for the most
				 * common value is useful information. If you don't like
				 * it, maybe you shouldn't be using eqsel for your
				 * operator...)
				 */
				RegProcedure eqproc = get_opcode(opid);
				bool		mostcommon;

				if (eqproc == (RegProcedure) NULL)
					elog(ERROR, "eqsel: no procedure for operator %lu",
						 opid);

				/* be careful to apply operator right way 'round */
				if (flag & SEL_RIGHT)
					mostcommon = (bool)
						DatumGetUInt8(fmgr(eqproc, commonval->value, value));
				else
					mostcommon = (bool)
						DatumGetUInt8(fmgr(eqproc, value, commonval->value));

				if (mostcommon)
				{

					/*
					 * Constant is "=" to the most common value.  We know
					 * selectivity exactly (or as exactly as VACUUM could
					 * calculate it, anyway).
					 */
					selec = commonfrac;
				}
				else
				{

					/*
					 * Comparison is against a constant that is neither
					 * the most common value nor null.	Its selectivity
					 * cannot be more than this:
					 */
					selec = 1.0 - commonfrac - nullfrac;
					if (selec > commonfrac)
						selec = commonfrac;

					/*
					 * and in fact it's probably less, so we should apply
					 * a fudge factor.	The only case where we don't is
					 * for a boolean column, where indeed we have
					 * estimated the less-common value's frequency
					 * exactly!
					 */
					if (typid != BOOLOID)
						selec *= NOT_MOST_COMMON_RATIO;
				}
			}
			else
			{

				/*
				 * Search is for a value that we do not know a priori, but
				 * we will assume it is not NULL.  Selectivity cannot be
				 * more than this:
				 */
				selec = 1.0 - nullfrac;
				if (selec > commonfrac)
					selec = commonfrac;

				/*
				 * and in fact it's probably less, so apply a fudge
				 * factor.
				 */
				selec *= NOT_MOST_COMMON_RATIO;
			}

			/* result should be in range, but make sure... */
			if (selec < 0.0)
				selec = 0.0;
			else if (selec > 1.0)
				selec = 1.0;

			pfree(commonval);
			
		}
		else
		{

			/*
			 * No VACUUM ANALYZE stats available, so make a guess using
			 * the disbursion stat (if we have that, which is unlikely for
			 * a normal attribute; but for a system attribute we may be
			 * able to estimate it).
			 */
			selec = get_attdisbursion(relid, attno, 0.01);
		}

		*result = (float64data) selec;
	}
	return result;
}

/*
 *		neqsel			- Selectivity of "!=" for any data types.
 *
 * This routine is also used for some operators that are not "!="
 * but have comparable selectivity behavior.  See above comments
 * for eqsel().
 */
float64
neqsel(Oid opid,
	   Oid relid,
	   AttrNumber attno,
	   Datum value,
	   int32 flag)
{
	float64		result;

	result = eqsel(opid, relid, attno, value, flag);
	*result = 1.0 - *result;
	return result;
}

/*
 *		scalarltsel		- Selectivity of "<" (also "<=") for scalars.
 *
 * This routine works for any datatype (or pair of datatypes) known to
 * convert_to_scalar().  If it is applied to some other datatype,
 * it will return a default estimate.
 */
float64
scalarltsel(Oid opid,
			Oid relid,
			AttrNumber attno,
			Datum value,
			int32 flag)
{
	float64		result;

	result = (float64) palloc(sizeof(float64data));
	if (!(flag & SEL_CONSTANT) || NONVALUE(attno) || NONVALUE(relid))
		*result = DEFAULT_INEQ_SEL;
	else
	{
		HeapTuple	oprtuple;
		Oid			ltype,
					rtype,
					contype;
		Oid			typid;
		int			typlen;
		bool		typbyval;
		int32		typmod;
		wrapped_datum		*hival,
					*loval;
		double		val,
					high,
					low,
					numerator,
					denominator;

		/*
		 * Get left and right datatypes of the operator so we know what
		 * type the constant is.
		 */
		oprtuple = get_operator_tuple(opid);
		if (!HeapTupleIsValid(oprtuple))
			elog(ERROR, "scalarltsel: no tuple for operator %lu", opid);
		ltype = ((Form_pg_operator) GETSTRUCT(oprtuple))->oprleft;
		rtype = ((Form_pg_operator) GETSTRUCT(oprtuple))->oprright;
		contype = (flag & SEL_RIGHT) ? rtype : ltype;

		/* Now get info and stats about the attribute */
		getattproperties(relid, attno,
						 &typid, &typlen, &typbyval, &typmod);

		if (!getattstatistics(relid, attno, typid, typmod,
							  NULL, NULL, NULL,
							  &loval, &hival))
		{
			/* no stats available, so default result */
			*result = DEFAULT_INEQ_SEL;
			return result;
		}

		/* Convert the values to a uniform comparison scale. */
		if (!convert_to_scalar(value, contype, &val,
							   loval->value, hival->value, typid,
							   &low, &high))
		{

			/*
			 * Ideally we'd produce an error here, on the grounds that the
			 * given operator shouldn't have scalarltsel registered as its
			 * selectivity func unless we can deal with its operand types.
			 * But currently, all manner of stuff is invoking scalarltsel,
			 * so give a default estimate until that can be fixed.
			 */
			pfree(hival);
			pfree(loval);
			
			*result = DEFAULT_INEQ_SEL;
			return result;
		}

                pfree(hival);
                pfree(loval);
		if (high <= low)
		{

			/*
			 * If we trusted the stats fully, we could return a small or
			 * large selec depending on which side of the single data
			 * point the constant is on.  But it seems better to assume
			 * that the stats are wrong and return a default...
			 */
			*result = DEFAULT_INEQ_SEL;
		}
		else if (val < low || val > high)
		{

			/*
			 * If given value is outside the statistical range, return a
			 * small or large value; but not 0.0/1.0 since there is a
			 * chance the stats are out of date.
			 */
			if (flag & SEL_RIGHT) {
				*result = (val < low) ? 0.001 : 0.999;
                        } else {
				*result = (val > high) ? 0.001 : 0.999;
                        }
		}
		else
		{
			denominator = high - low;
			if (flag & SEL_RIGHT)
				numerator = val - low;
			else
				numerator = high - val;
			*result = numerator / denominator;
		}
	}
	return result;
}

/*
 *		scalargtsel		- Selectivity of ">" (also ">=") for integers.
 *
 * See above comments for scalarltsel.
 */
float64
scalargtsel(Oid opid,
			Oid relid,
			AttrNumber attno,
			Datum value,
			int32 flag)
{
	float64		result;

	/*
	 * Compute selectivity of "<", then invert --- but only if we were
	 * able to produce a non-default estimate.
	 */
	result = scalarltsel(opid, relid, attno, value, flag);
	if (*result != DEFAULT_INEQ_SEL)
		*result = 1.0 - *result;
	return result;
}

/*
 * patternsel			- Generic code for pattern-match selectivity.
 */
static float64
patternsel(Oid opid,
		   Pattern_Type ptype,
		   Oid relid,
		   AttrNumber attno,
		   Datum value,
		   int32 flag)
{
	float64		result;

	result = (float64) palloc(sizeof(float64data));
	/* Must have a constant for the pattern, or cannot learn anything */
	if ((flag & (SEL_CONSTANT | SEL_RIGHT)) != (SEL_CONSTANT | SEL_RIGHT))
		*result = DEFAULT_MATCH_SEL;
	else
	{
		HeapTuple	oprtuple;
		Oid			ltype,
					rtype;
		char	   *patt;
		Pattern_Prefix_Status pstatus;
		char	   *prefix;
		char	   *rest;

		/*
		 * Get left and right datatypes of the operator so we know what
		 * type the attribute is.
		 */
		oprtuple = get_operator_tuple(opid);
		if (!HeapTupleIsValid(oprtuple))
			elog(ERROR, "patternsel: no tuple for operator %lu", opid);
		ltype = ((Form_pg_operator) GETSTRUCT(oprtuple))->oprleft;
		rtype = ((Form_pg_operator) GETSTRUCT(oprtuple))->oprright;

		/* the right-hand const is type text for all supported operators */
		Assert(rtype == TEXTOID);
		patt = textout((text *) DatumGetPointer(value));

		/* divide pattern into fixed prefix and remainder */
		pstatus = pattern_fixed_prefix(patt, ptype, &prefix, &rest);

		if (pstatus == Pattern_Prefix_Exact)
		{
			/* Pattern specifies an exact match, so pretend operator is '=' */
			Oid		eqopr = find_operator("=", ltype);
			Datum	eqcon;

			if (eqopr == InvalidOid)
				elog(ERROR, "patternsel: no = operator for type %lu", ltype);
			eqcon = string_to_datum(prefix, ltype);
			result = eqsel(eqopr, relid, attno, eqcon, SEL_CONSTANT|SEL_RIGHT);
			pfree(DatumGetPointer(eqcon));
		}
		else
		{
			/*
			 * Not exact-match pattern.  We estimate selectivity of the
			 * fixed prefix and remainder of pattern separately, then
			 * combine the two.
			 */
			Selectivity prefixsel;
			Selectivity restsel;
			Selectivity selec;

			if (pstatus == Pattern_Prefix_Partial)
				prefixsel = prefix_selectivity(prefix, relid, attno, ltype);
			else
				prefixsel = 1.0;
			restsel = pattern_selectivity(rest, ptype);
			selec = prefixsel * restsel;
			/* result should be in range, but make sure... */
			if (selec < 0.0)
				selec = 0.0;
			else if (selec > 1.0)
				selec = 1.0;
			*result = (float64data) selec;
		}
		if (prefix)
			pfree(prefix);
		pfree(patt);
	}
	return result;
}

/*
 *		regexeqsel		- Selectivity of regular-expression pattern match.
 */
float64
regexeqsel(Oid opid,
		   Oid relid,
		   AttrNumber attno,
		   Datum value,
		   int32 flag)
{
	return patternsel(opid, Pattern_Type_Regex, relid, attno, value, flag);
}

/*
 *		icregexeqsel	- Selectivity of case-insensitive regex match.
 */
float64
icregexeqsel(Oid opid,
			 Oid relid,
			 AttrNumber attno,
			 Datum value,
			 int32 flag)
{
	return patternsel(opid, Pattern_Type_Regex_IC, relid, attno, value, flag);
}

/*
 *		likesel			- Selectivity of LIKE pattern match.
 */
float64
likesel(Oid opid,
		Oid relid,
		AttrNumber attno,
		Datum value,
		int32 flag)
{
	return patternsel(opid, Pattern_Type_Like, relid, attno, value, flag);
}

/*
 *		regexnesel		- Selectivity of regular-expression pattern non-match.
 */
float64
regexnesel(Oid opid,
		   Oid relid,
		   AttrNumber attno,
		   Datum value,
		   int32 flag)
{
	float64		result;

	result = patternsel(opid, Pattern_Type_Regex, relid, attno, value, flag);
	*result = 1.0 - *result;
	return result;
}

/*
 *		icregexnesel	- Selectivity of case-insensitive regex non-match.
 */
float64
icregexnesel(Oid opid,
			 Oid relid,
			 AttrNumber attno,
			 Datum value,
			 int32 flag)
{
	float64		result;

	result = patternsel(opid, Pattern_Type_Regex_IC, relid, attno, value, flag);
	*result = 1.0 - *result;
	return result;
}

/*
 *		nlikesel		- Selectivity of LIKE pattern non-match.
 */
float64
nlikesel(Oid opid,
		 Oid relid,
		 AttrNumber attno,
		 Datum value,
		 int32 flag)
{
	float64		result;

	result = patternsel(opid, Pattern_Type_Like, relid, attno, value, flag);
	*result = 1.0 - *result;
	return result;
}

/*
 *		eqjoinsel		- Join selectivity of "="
 */
float64
eqjoinsel(Oid opid,
		  Oid relid1,
		  AttrNumber attno1,
		  Oid relid2,
		  AttrNumber attno2)
{
	float64		result;
	float64data num1,
				num2,
				min;
	bool		unknown1 = NONVALUE(relid1) || NONVALUE(attno1);
	bool		unknown2 = NONVALUE(relid2) || NONVALUE(attno2);

	result = (float64) palloc(sizeof(float64data));
	if (unknown1 && unknown2)
		*result = DEFAULT_EQ_SEL;
	else
	{
		num1 = unknown1 ? 1.0 : get_attdisbursion(relid1, attno1, 0.01);
		num2 = unknown2 ? 1.0 : get_attdisbursion(relid2, attno2, 0.01);

		/*
		 * The join selectivity cannot be more than num2, since each tuple
		 * in table 1 could match no more than num2 fraction of tuples in
		 * table 2 (and that's only if the table-1 tuple matches the most
		 * common value in table 2, so probably it's less).  By the same
		 * reasoning it is not more than num1. The min is therefore an
		 * upper bound.
		 *
		 * If we know the disbursion of only one side, use it; the reasoning
		 * above still works.
		 *
		 * XXX can we make a better estimate here?	Using the nullfrac
		 * statistic might be helpful, for example.  Assuming the operator
		 * is strict (does not succeed for null inputs) then the
		 * selectivity couldn't be more than (1-nullfrac1)*(1-nullfrac2),
		 * which might be usefully small if there are many nulls.  How
		 * about applying the operator to the most common values?
		 */
		min = (num1 < num2) ? num1 : num2;
		*result = min;
	}
	return result;
}

/*
 *		neqjoinsel		- Join selectivity of "!="
 */
float64
neqjoinsel(Oid opid,
		   Oid relid1,
		   AttrNumber attno1,
		   Oid relid2,
		   AttrNumber attno2)
{
	float64		result;

	result = eqjoinsel(opid, relid1, attno1, relid2, attno2);
	*result = 1.0 - *result;
	return result;
}

/*
 *		scalarltjoinsel - Join selectivity of "<" and "<=" for scalars
 */
float64
scalarltjoinsel(Oid opid,
				Oid relid1,
				AttrNumber attno1,
				Oid relid2,
				AttrNumber attno2)
{
	float64		result;

	result = (float64) palloc(sizeof(float64data));
	*result = DEFAULT_INEQ_SEL;
	return result;
}

/*
 *		scalargtjoinsel - Join selectivity of ">" and ">=" for scalars
 */
float64
scalargtjoinsel(Oid opid,
				Oid relid1,
				AttrNumber attno1,
				Oid relid2,
				AttrNumber attno2)
{
	float64		result;

	result = (float64) palloc(sizeof(float64data));
	*result = DEFAULT_INEQ_SEL;
	return result;
}

/*
 *		regexeqjoinsel	- Join selectivity of regular-expression pattern match.
 */
float64
regexeqjoinsel(Oid opid,
			   Oid relid1,
			   AttrNumber attno1,
			   Oid relid2,
			   AttrNumber attno2)
{
	float64		result;

	result = (float64) palloc(sizeof(float64data));
	*result = DEFAULT_MATCH_SEL;
	return result;
}

/*
 *		icregexeqjoinsel	- Join selectivity of case-insensitive regex match.
 */
float64
icregexeqjoinsel(Oid opid,
				 Oid relid1,
				 AttrNumber attno1,
				 Oid relid2,
				 AttrNumber attno2)
{
	float64		result;

	result = (float64) palloc(sizeof(float64data));
	*result = DEFAULT_MATCH_SEL;
	return result;
}

/*
 *		likejoinsel			- Join selectivity of LIKE pattern match.
 */
float64
likejoinsel(Oid opid,
			Oid relid1,
			AttrNumber attno1,
			Oid relid2,
			AttrNumber attno2)
{
	float64		result;

	result = (float64) palloc(sizeof(float64data));
	*result = DEFAULT_MATCH_SEL;
	return result;
}

/*
 *		regexnejoinsel	- Join selectivity of regex non-match.
 */
float64
regexnejoinsel(Oid opid,
			   Oid relid1,
			   AttrNumber attno1,
			   Oid relid2,
			   AttrNumber attno2)
{
	float64		result;

	result = regexeqjoinsel(opid, relid1, attno1, relid2, attno2);
	*result = 1.0 - *result;
	return result;
}

/*
 *		icregexnejoinsel	- Join selectivity of case-insensitive regex non-match.
 */
float64
icregexnejoinsel(Oid opid,
				 Oid relid1,
				 AttrNumber attno1,
				 Oid relid2,
				 AttrNumber attno2)
{
	float64		result;

	result = icregexeqjoinsel(opid, relid1, attno1, relid2, attno2);
	*result = 1.0 - *result;
	return result;
}

/*
 *		nlikejoinsel		- Join selectivity of LIKE pattern non-match.
 */
float64
nlikejoinsel(Oid opid,
			 Oid relid1,
			 AttrNumber attno1,
			 Oid relid2,
			 AttrNumber attno2)
{
	float64		result;

	result = likejoinsel(opid, relid1, attno1, relid2, attno2);
	*result = 1.0 - *result;
	return result;
}


/*
 * convert_to_scalar
 *	  Convert non-NULL values of the indicated types to the comparison
 *	  scale needed by scalarltsel()/scalargtsel().
 *	  Returns "true" if successful.
 *
 * All numeric datatypes are simply converted to their equivalent
 * "double" values.
 *
 * String datatypes are converted by convert_string_to_scalar(),
 * which is explained below.  The reason why this routine deals with
 * three values at a time, not just one, is that we need it for strings.
 *
 * The several datatypes representing absolute times are all converted
 * to Timestamp, which is actually a double, and then we just use that
 * double value.  Note this will give bad results for the various "special"
 * values of Timestamp --- what can we do with those?
 *
 * The several datatypes representing relative times (intervals) are all
 * converted to measurements expressed in seconds.
 */
static bool
convert_to_scalar(Datum value, Oid valuetypid, double *scaledvalue,
				  Datum lobound, Datum hibound, Oid boundstypid,
				  double *scaledlobound, double *scaledhibound)
{
	switch (valuetypid)
	{

		/*
		 * Built-in numeric types
		 */
		case BOOLOID:
		case INT2OID:
		case INT4OID:
		case INT8OID:
		case FLOAT4OID:
		case FLOAT8OID:
		case NUMERICOID:
		case CONNECTOROID:
		case OIDOID:
		case REGPROCOID:
			*scaledvalue = convert_numeric_to_scalar(value, valuetypid);
			*scaledlobound = convert_numeric_to_scalar(lobound, boundstypid);
			*scaledhibound = convert_numeric_to_scalar(hibound, boundstypid);
			return true;

		/*
		 * Built-in string types
		 */
		case CHAROID:
		case BPCHAROID:
		case VARCHAROID:
		case TEXTOID:
		case NAMEOID:
		{
			unsigned char *valstr = ( value ) ? convert_string_datum(value, valuetypid) : NULL;
			unsigned char *lostr = ( lobound ) ? convert_string_datum(lobound, boundstypid) : NULL;
			unsigned char *histr = ( hibound ) ? convert_string_datum(hibound, boundstypid) : NULL;

			if ( valstr && lostr && histr ) {
				convert_string_to_scalar(valstr, scaledvalue,
									 lostr, scaledlobound,
									 histr, scaledhibound);
			} 

			if ( valstr ) pfree(valstr);
			if ( lostr ) pfree(lostr);
			if ( histr ) pfree(histr);
			return true;
		}

		/*
		 * Built-in time types
		 */
		case TIMESTAMPOID:
		case ABSTIMEOID:
		case DATEOID:
		case INTERVALOID:
		case RELTIMEOID:
		case TINTERVALOID:
		case TIMEOID:
			*scaledvalue = convert_timevalue_to_scalar(value, valuetypid);
			*scaledlobound = convert_timevalue_to_scalar(lobound, boundstypid);
			*scaledhibound = convert_timevalue_to_scalar(hibound, boundstypid);
			return true;
                case JAVAOID:
                    return convert_java_to_scalar(value,scaledvalue,lobound,scaledlobound,hibound,scaledhibound, PointerGetDatum(NULL));
	}
	/* Don't know how to convert */
	return false;
}

/*
 * Do convert_to_scalar()'s work for any numeric data type.
 */
static double
convert_numeric_to_scalar(Datum value, Oid typid)
{
	switch (typid)
	{
		case BOOLOID:
			return (double) DatumGetUInt8(value);
		case INT2OID:
			return (double) DatumGetInt16(value);
		case INT4OID:
			return (double) DatumGetInt32(value);
		case INT8OID:
			return (double) (*i8tod((int64 *) DatumGetPointer(value)));
		case FLOAT4OID:
			return (double) (*DatumGetFloat32(value));
		case FLOAT8OID:
			return (double) (*DatumGetFloat64(value));
		case NUMERICOID:
			return (double) (*numeric_float8((Numeric) DatumGetPointer(value)));
                case CONNECTOROID:
                        return (double) DatumGetInt32(value);
		case OIDOID:
		case REGPROCOID:
			/* we can treat OIDs as integers... */
			return (double) DatumGetObjectId(value);
	}
	/* Can't get here unless someone tries to use scalarltsel/scalargtsel
	 * on an operator with one numeric and one non-numeric operand.
	 */
	elog(ERROR, "convert_numeric_to_scalar: unsupported type %lu", typid);
	return 0;
}

/*
 * Do convert_to_scalar()'s work for any character-string data type.
 *
 * String datatypes are converted to a scale that ranges from 0 to 1,
 * where we visualize the bytes of the string as fractional digits.
 *
 * We do not want the base to be 256, however, since that tends to
 * generate inflated selectivity estimates; few databases will have
 * occurrences of all 256 possible byte values at each position.
 * Instead, use the smallest and largest byte values seen in the bounds
 * as the estimated range for each byte, after some fudging to deal with
 * the fact that we probably aren't going to see the full range that way.
 *
 * An additional refinement is that we discard any common prefix of the
 * three strings before computing the scaled values.  This allows us to
 * "zoom in" when we encounter a narrow data range.  An example is a phone
 * number database where all the values begin with the same area code.
 */
static void
convert_string_to_scalar(unsigned char *value,
						 double *scaledvalue,
						 unsigned char *lobound,
						 double *scaledlobound,
						 unsigned char *hibound,
						 double *scaledhibound)
{
	int			rangelo,
				rangehi;
	unsigned char *sptr;

	rangelo = rangehi = hibound[0];
	for (sptr = lobound; *sptr; sptr++)
	{
		if (rangelo > *sptr)
			rangelo = *sptr;
		if (rangehi < *sptr)
			rangehi = *sptr;
	}
	for (sptr = hibound; *sptr; sptr++)
	{
		if (rangelo > *sptr)
			rangelo = *sptr;
		if (rangehi < *sptr)
			rangehi = *sptr;
	}
	/* If range includes any upper-case ASCII chars, make it include all */
	if (rangelo <= 'Z' && rangehi >= 'A')
	{
		if (rangelo > 'A')
			rangelo = 'A';
		if (rangehi < 'Z')
			rangehi = 'Z';
	}
	/* Ditto lower-case */
	if (rangelo <= 'z' && rangehi >= 'a')
	{
		if (rangelo > 'a')
			rangelo = 'a';
		if (rangehi < 'z')
			rangehi = 'z';
	}
	/* Ditto digits */
	if (rangelo <= '9' && rangehi >= '0')
	{
		if (rangelo > '0')
			rangelo = '0';
		if (rangehi < '9')
			rangehi = '9';
	}
	/* If range includes less than 10 chars, assume we have not got enough
	 * data, and make it include regular ASCII set.
	 */
	if (rangehi - rangelo < 9)
	{
		rangelo = ' ';
		rangehi = 127;
	}

	/*
	 * Now strip any common prefix of the three strings.
	 */
	while (*lobound)
	{
		if (*lobound != *hibound || *lobound != *value)
			break;
		lobound++, hibound++, value++;
	}

	/*
	 * Now we can do the conversions.
	 */
	*scaledvalue = convert_one_string_to_scalar(value, rangelo, rangehi);
	*scaledlobound = convert_one_string_to_scalar(lobound, rangelo, rangehi);
	*scaledhibound = convert_one_string_to_scalar(hibound, rangelo, rangehi);
}

static double
convert_one_string_to_scalar(unsigned char *value, int rangelo, int rangehi)
{
	int			slen = strlen((char *) value);
	double		num,
				denom,
				base;

	if (slen <= 0)
		return 0.0;				/* empty string has scalar value 0 */

	/* Since base is at least 10, need not consider more than about 20 chars */
	if (slen > 20)
		slen = 20;

	/* Convert initial characters to fraction */
	base = rangehi - rangelo + 1;
	num = 0.0;
	denom = base;
	while (slen-- > 0)
	{
		int		ch = *value++;

		if (ch < rangelo)
			ch = rangelo-1;
		else if (ch > rangehi)
			ch = rangehi+1;
		num += ((double) (ch - rangelo)) / denom;
		denom *= base;
	}

	return num;
}

/*
 * Convert a string-type Datum into a palloc'd, null-terminated string.
 *
 * If USE_LOCALE is defined, we must pass the string through strxfrm()
 * before continuing, so as to generate correct locale-specific results.
 */
static unsigned char *
convert_string_datum(Datum value, Oid typid)
{
	char	   *val;
#ifdef USE_LOCALE
	char	   *xfrmstr;
	size_t		xfrmsize;
	size_t		xfrmlen;
#endif

	switch (typid)
	{
		case CHAROID:
			val = (char *) palloc(2);
			val[0] = DatumGetChar(value);
			val[1] = '\0';
			break;
		case BPCHAROID:
		case VARCHAROID:
		case TEXTOID:
		{
			char	   *str = (char *) VARDATA(DatumGetPointer(value));
			int			strlength = VARSIZE(DatumGetPointer(value)) - VARHDRSZ;

			val = (char *) palloc(strlength+1);
			memcpy(val, str, strlength);
			val[strlength] = '\0';
			break;
		}
		case NAMEOID:
		{
			NameData   *nm = (NameData *) DatumGetPointer(value);

			val = pstrdup(NameStr(*nm));
			break;
		}
		default:
			/* Can't get here unless someone tries to use scalarltsel
			 * on an operator with one string and one non-string operand.
			 */
			elog(ERROR, "convert_string_datum: unsupported type %lu", typid);
			return NULL;
	}

#ifdef USE_LOCALE
	/* Guess that transformed string is not much bigger than original */
	xfrmsize = strlen(val) + 32;		/* arbitrary pad value here... */
	xfrmstr = (char *) palloc(xfrmsize);
	xfrmlen = strxfrm(xfrmstr, val, xfrmsize);
	if (xfrmlen >= xfrmsize)
	{
		/* Oops, didn't make it */
		pfree(xfrmstr);
		xfrmstr = (char *) palloc(xfrmlen + 1);
		xfrmlen = strxfrm(xfrmstr, val, xfrmlen + 1);
	}
	pfree(val);
	val = xfrmstr;
#endif

	return (unsigned char *) val;
}

/*
 * Do convert_to_scalar()'s work for any timevalue data type.
 */
static double
convert_timevalue_to_scalar(Datum value, Oid typid)
{
	switch (typid)
	{
		case TIMESTAMPOID:
			return *((Timestamp *) DatumGetPointer(value));
		case ABSTIMEOID:
			return *abstime_timestamp(value);
		case DATEOID:
			return *date_timestamp(value);
		case INTERVALOID:
		{
			Interval   *interval = (Interval *) DatumGetPointer(value);

			/*
			 * Convert the month part of Interval to days using
			 * assumed average month length of 365.25/12.0 days.  Not
			 * too accurate, but plenty good enough for our purposes.
			 */
			return interval->time +
				interval->month * (365.25 / 12.0 * 24.0 * 60.0 * 60.0);
		}
		case RELTIMEOID:
			return (RelativeTime) DatumGetInt32(value);
		case TINTERVALOID:
		{
			TimeInterval interval = (TimeInterval) DatumGetPointer(value);

			if (interval->status != 0)
				return interval->data[1] - interval->data[0];
			return 0;			/* for lack of a better idea */
		}
		case TIMEOID:
			return *((TimeADT *) DatumGetPointer(value));
	}
	/* Can't get here unless someone tries to use scalarltsel/scalargtsel
	 * on an operator with one timevalue and one non-timevalue operand.
	 */
	elog(ERROR, "convert_timevalue_to_scalar: unsupported type %lu", typid);
	return 0;
}


/*
 * getattproperties
 *	  Retrieve pg_attribute properties for an attribute,
 *	  including type OID, type len, type byval flag, typmod.
 */
static void
getattproperties(Oid relid, AttrNumber attnum,
				 Oid *typid, int *typlen, bool *typbyval, int32 *typmod)
{
	HeapTuple	atp;
	Form_pg_attribute att_tup;

	atp = SearchSysCacheTuple(ATTNUM,
							  ObjectIdGetDatum(relid),
							  Int16GetDatum(attnum),
							  0, 0);
	if (!HeapTupleIsValid(atp))
		elog(ERROR, "getattproperties: no attribute tuple %lu %d",
			 relid, (int) attnum);
	att_tup = (Form_pg_attribute) GETSTRUCT(atp);

	*typid = att_tup->atttypid;
	*typlen = att_tup->attlen;
	*typbyval = att_tup->attbyval;
	*typmod = att_tup->atttypmod;
}

/*
 * getattstatistics
 *	  Retrieve the pg_statistic data for an attribute.
 *	  Returns 'false' if no stats are available.
 *
 * Inputs:
 * 'relid' and 'attnum' are the relation and attribute number.
 * 'typid' and 'typmod' are the type and typmod of the column,
 * which the caller must already have looked up.
 *
 * Outputs:
 * The available stats are nullfrac, commonfrac, commonval, loval, hival.
 * The caller need not retrieve all five --- pass NULL pointers for the
 * unwanted values.
 *
 * commonval, loval, hival are returned as Datums holding the internal
 * representation of the values.  (Note that these should be pfree'd
 * after use if the data type is not by-value.)
 */
static bool
getattstatistics(Oid relid,
				 AttrNumber attnum,
				 Oid typid,
				 int32 typmod,
				 double *nullfrac,
				 double *commonfrac,
				 wrapped_datum **commonval,
				 wrapped_datum **loval,
				 wrapped_datum **hival)
{
	HeapTuple	tuple;
	HeapTuple	typeTuple;
	FmgrInfo	inputproc;
	bool		isnull;

	/*
	 * We assume that there will only be one entry in pg_statistic for the
	 * given rel/att, so we search WITHOUT considering the staop column.
	 * Someday, VACUUM might store more than one entry per rel/att,
	 * corresponding to more than one possible sort ordering defined for
	 * the column type.  However, to make that work we will need to figure
	 * out which staop to search for --- it's not necessarily the one we
	 * have at hand!  (For example, we might have a '>' operator rather
	 * than the '<' operator that will appear in staop.)
	 */
	tuple = SearchSysCacheTuple(STATRELID,
								ObjectIdGetDatum(relid),
								Int16GetDatum((int16) attnum),
								0,
								0);
	if (!HeapTupleIsValid(tuple))
	{
		/* no such stats entry */
		return false;
	}

	if (nullfrac)
		*nullfrac = ((Form_pg_statistic) GETSTRUCT(tuple))->stanullfrac;
	if (commonfrac)
		*commonfrac = ((Form_pg_statistic) GETSTRUCT(tuple))->stacommonfrac;

	/* Get the type input proc for the column datatype */
	typeTuple = SearchSysCacheTuple(TYPEOID,
									ObjectIdGetDatum(typid),
									0, 0, 0);
	if (!HeapTupleIsValid(typeTuple))
		elog(ERROR, "getattstatistics: Cache lookup failed for type %lu",
			 typid);
	fmgr_info(((Form_pg_type) GETSTRUCT(typeTuple))->typinput, &inputproc);

	/*
	 * Values are variable-length fields, so cannot access as struct
	 * fields. Must do it the hard way with SysCacheGetAttr.
	 */
	if (commonval)
	{
		bytea	   *val = (bytea *) SysCacheGetAttr(STATRELID, tuple,
										  Anum_pg_statistic_stacommonval,
												   &isnull);

		if (isnull)
		{
			*commonval = NULL;
			return false;
			
		}
		else
		{
			*commonval = wrappedout(val);
		}
	}

	if (loval)
	{
		bytea	   *val = (bytea *) SysCacheGetAttr(STATRELID, tuple,
											  Anum_pg_statistic_staloval,
												   &isnull);

		if (isnull)
		{
			*loval = NULL;
			return false;
		}
		else
		{
			*loval = wrappedout(val);
		}
	}

	if (hival)
	{
		bytea	   *val = (bytea *) SysCacheGetAttr(STATRELID, tuple,
											  Anum_pg_statistic_stahival,
												   &isnull);

		if (isnull)
		{
			*hival = NULL;
			return false;
		}
		else
		{
			*hival = wrappedout(val);
		}
	}

	return true;
}

/*-------------------------------------------------------------------------
 *
 * Pattern analysis functions
 *
 * These routines support analysis of LIKE and regular-expression patterns
 * by the planner/optimizer.  It's important that they agree with the
 * regular-expression code in backend/regex/ and the LIKE code in
 * backend/utils/adt/like.c.
 *
 * Note that the prefix-analysis functions are called from
 * backend/optimizer/path/indxpath.c as well as from routines in this file.
 *
 *-------------------------------------------------------------------------
 */

/*
 * Extract the fixed prefix, if any, for a pattern.
 * *prefix is set to a palloc'd prefix string,
 * or to NULL if no fixed prefix exists for the pattern.
 * *rest is set to point to the remainder of the pattern after the
 * portion describing the fixed prefix.
 * The return value distinguishes no fixed prefix, a partial prefix,
 * or an exact-match-only pattern.
 */

static Pattern_Prefix_Status
like_fixed_prefix(char *patt, char **prefix, char **rest)
{
	char	   *match;
	int			pos,
				match_pos;

	*prefix = match = palloc(strlen(patt) + 1);
	match_pos = 0;

	for (pos = 0; patt[pos]; pos++)
	{
		/* % and _ are wildcard characters in LIKE */
		if (patt[pos] == '%' ||
			patt[pos] == '_')
			break;
		/* Backslash quotes the next character */
		if (patt[pos] == '\\')
		{
			pos++;
			if (patt[pos] == '\0')
				break;
		}

		/*
		 * NOTE: this code used to think that %% meant a literal %, but
		 * textlike() itself does not think that, and the SQL92 spec
		 * doesn't say any such thing either.
		 */
		match[match_pos++] = patt[pos];
	}

	match[match_pos] = '\0';
	*rest = &patt[pos];

	/* in LIKE, an empty pattern is an exact match! */
	if (patt[pos] == '\0')
		return Pattern_Prefix_Exact;	/* reached end of pattern, so exact */

	if (match_pos > 0)
		return Pattern_Prefix_Partial;

	pfree(match);
	*prefix = NULL;
	return Pattern_Prefix_None;
}

static Pattern_Prefix_Status
regex_fixed_prefix(char *patt, bool case_insensitive,
				   char **prefix, char **rest)
{
	char	   *match;
	int			pos,
				match_pos,
				paren_depth;

	/* Pattern must be anchored left */
	if (patt[0] != '^')
	{
		*prefix = NULL;
		*rest = patt;
		return Pattern_Prefix_None;
	}

	/* If unquoted | is present at paren level 0 in pattern, then there
	 * are multiple alternatives for the start of the string.
	 */
	paren_depth = 0;
	for (pos = 1; patt[pos]; pos++)
	{
		if (patt[pos] == '|' && paren_depth == 0)
		{
			*prefix = NULL;
			*rest = patt;
			return Pattern_Prefix_None;
		}
		else if (patt[pos] == '(')
			paren_depth++;
		else if (patt[pos] == ')' && paren_depth > 0)
			paren_depth--;
		else if (patt[pos] == '\\')
		{
			/* backslash quotes the next character */
			pos++;
			if (patt[pos] == '\0')
				break;
		}
	}

	/* OK, allocate space for pattern */
	*prefix = match = palloc(strlen(patt) + 1);
	match_pos = 0;

	/* note start at pos 1 to skip leading ^ */
	for (pos = 1; patt[pos]; pos++)
	{
		/*
		 * Check for characters that indicate multiple possible matches here.
		 * XXX I suspect isalpha() is not an adequately locale-sensitive
		 * test for characters that can vary under case folding?
		 */
		if (patt[pos] == '.' ||
			patt[pos] == '(' ||
			patt[pos] == '[' ||
			patt[pos] == '$' ||
			(case_insensitive && isalpha(patt[pos])))
			break;
		/*
		 * Check for quantifiers.  Except for +, this means the preceding
		 * character is optional, so we must remove it from the prefix too!
		 */
		if (patt[pos] == '*' ||
			patt[pos] == '?' ||
			patt[pos] == '{')
		{
			if (match_pos > 0)
				match_pos--;
			pos--;
			break;
		}
		if (patt[pos] == '+')
		{
			pos--;
			break;
		}
		if (patt[pos] == '\\')
		{
			/* backslash quotes the next character */
			pos++;
			if (patt[pos] == '\0')
				break;
		}
		match[match_pos++] = patt[pos];
	}

	match[match_pos] = '\0';
	*rest = &patt[pos];

	if (patt[pos] == '$' && patt[pos + 1] == '\0')
	{
		*rest = &patt[pos + 1];
		return Pattern_Prefix_Exact;	/* pattern specifies exact match */
	}

	if (match_pos > 0)
		return Pattern_Prefix_Partial;

	pfree(match);
	*prefix = NULL;
	return Pattern_Prefix_None;
}

Pattern_Prefix_Status
pattern_fixed_prefix(char *patt, Pattern_Type ptype,
					 char **prefix, char **rest)
{
	Pattern_Prefix_Status result;

	switch (ptype)
	{
		case Pattern_Type_Like:
			result = like_fixed_prefix(patt, prefix, rest);
			break;
		case Pattern_Type_Regex:
			result = regex_fixed_prefix(patt, false, prefix, rest);
			break;
		case Pattern_Type_Regex_IC:
			result = regex_fixed_prefix(patt, true, prefix, rest);
			break;
		default:
			elog(ERROR, "pattern_fixed_prefix: bogus ptype");
			result = Pattern_Prefix_None; /* keep compiler quiet */
			break;
	}
	return result;
}

/*
 * Estimate the selectivity of a fixed prefix for a pattern match.
 *
 * A fixed prefix "foo" is estimated as the selectivity of the expression
 * "var >= 'foo' AND var < 'fop'" (see also indxqual.c).
 */
static Selectivity
prefix_selectivity(char *prefix,
				   Oid relid,
				   AttrNumber attno,
				   Oid datatype)
{
	Selectivity	prefixsel;
	Oid			cmpopr;
	Datum		prefixcon;
	char	   *greaterstr;

	cmpopr = find_operator(">=", datatype);
	if (cmpopr == InvalidOid)
		elog(ERROR, "prefix_selectivity: no >= operator for type %lu",
			 datatype);
	prefixcon = string_to_datum(prefix, datatype);
	/* Assume scalargtsel is appropriate for all supported types */
	prefixsel = * scalargtsel(cmpopr, relid, attno,
							  prefixcon, SEL_CONSTANT|SEL_RIGHT);
	pfree(DatumGetPointer(prefixcon));

	/*
	 * If we can create a string larger than the prefix,
	 * say "x < greaterstr".
	 */
	greaterstr = make_greater_string(prefix, datatype);
	if (greaterstr)
	{
		Selectivity		topsel;

		cmpopr = find_operator("<", datatype);
		if (cmpopr == InvalidOid)
			elog(ERROR, "prefix_selectivity: no < operator for type %lu",
				 datatype);
		prefixcon = string_to_datum(greaterstr, datatype);
		/* Assume scalarltsel is appropriate for all supported types */
		topsel = * scalarltsel(cmpopr, relid, attno,
							   prefixcon, SEL_CONSTANT|SEL_RIGHT);
		pfree(DatumGetPointer(prefixcon));
		pfree(greaterstr);

		/*
		 * Merge the two selectivities in the same way as for
		 * a range query (see clauselist_selectivity()).
		 */
		prefixsel = topsel + prefixsel - 1.0;

		/*
		 * A zero or slightly negative prefixsel should be converted into a
		 * small positive value; we probably are dealing with a very
		 * tight range and got a bogus result due to roundoff errors.
		 * However, if prefixsel is very negative, then we probably have
		 * default selectivity estimates on one or both sides of the
		 * range.  In that case, insert a not-so-wildly-optimistic
		 * default estimate.
		 */
		if (prefixsel <= 0.0)
		{
			if (prefixsel < -0.01)
			{

				/*
				 * No data available --- use a default estimate that
				 * is small, but not real small.
				 */
				prefixsel = 0.01;
			}
			else
			{

				/*
				 * It's just roundoff error; use a small positive value
				 */
				prefixsel = 1.0e-10;
			}
		}
	}

	return prefixsel;
}


/*
 * Estimate the selectivity of a pattern of the specified type.
 * Note that any fixed prefix of the pattern will have been removed already.
 *
 * For now, we use a very simplistic approach: fixed characters reduce the
 * selectivity a good deal, character ranges reduce it a little,
 * wildcards (such as % for LIKE or .* for regex) increase it.
 */

#define FIXED_CHAR_SEL	0.04	/* about 1/25 */
#define CHAR_RANGE_SEL	0.25
#define ANY_CHAR_SEL	0.9		/* not 1, since it won't match end-of-string */
#define FULL_WILDCARD_SEL 5.0
#define PARTIAL_WILDCARD_SEL 2.0

static Selectivity
like_selectivity(char *patt)
{
	Selectivity		sel = 1.0;
	int				pos;

	/* Skip any leading %; it's already factored into initial sel */
	pos = (*patt == '%') ? 1 : 0;
	for (; patt[pos]; pos++)
	{
		/* % and _ are wildcard characters in LIKE */
		if (patt[pos] == '%')
			sel *= FULL_WILDCARD_SEL;
		else if (patt[pos] == '_')
			sel *= ANY_CHAR_SEL;
		else if (patt[pos] == '\\')
		{
			/* Backslash quotes the next character */
			pos++;
			if (patt[pos] == '\0')
				break;
			sel *= FIXED_CHAR_SEL;
		}
		else
			sel *= FIXED_CHAR_SEL;
	}
	/* Could get sel > 1 if multiple wildcards */
	if (sel > 1.0)
		sel = 1.0;
	return sel;
}

static Selectivity
regex_selectivity_sub(char *patt, int pattlen, bool case_insensitive)
{
	Selectivity		sel = 1.0;
	int				paren_depth = 0;
	int				paren_pos = 0; /* dummy init to keep compiler quiet */
	int				pos;

	for (pos = 0; pos < pattlen; pos++)
	{
		if (patt[pos] == '(')
		{
			if (paren_depth == 0)
				paren_pos = pos; /* remember start of parenthesized item */
			paren_depth++;
		}
		else if (patt[pos] == ')' && paren_depth > 0)
		{
			paren_depth--;
			if (paren_depth == 0)
				sel *= regex_selectivity_sub(patt + (paren_pos + 1),
											 pos - (paren_pos + 1),
											 case_insensitive);
		}
		else if (patt[pos] == '|' && paren_depth == 0)
		{
			/*
			 * If unquoted | is present at paren level 0 in pattern,
			 * we have multiple alternatives; sum their probabilities.
			 */
			sel += regex_selectivity_sub(patt + (pos + 1),
										 pattlen - (pos + 1),
										 case_insensitive);
			break;				/* rest of pattern is now processed */
		}
		else if (patt[pos] == '[')
		{
			bool	negclass = false;

			if (patt[++pos] == '^')
			{
				negclass = true;
				pos++;
			}
			if (patt[pos] == ']') /* ']' at start of class is not special */
				pos++;
			while (pos < pattlen && patt[pos] != ']')
				pos++;
			if (paren_depth == 0)
				sel *= (negclass ? (1.0-CHAR_RANGE_SEL) : CHAR_RANGE_SEL);
		}
		else if (patt[pos] == '.')
		{
			if (paren_depth == 0)
				sel *= ANY_CHAR_SEL;
		}
		else if (patt[pos] == '*' ||
				 patt[pos] == '?' ||
				 patt[pos] == '+')
		{
			/* Ought to be smarter about quantifiers... */
			if (paren_depth == 0)
				sel *= PARTIAL_WILDCARD_SEL;
		}
		else if (patt[pos] == '{')
		{
			while (pos < pattlen && patt[pos] != '}')
				pos++;
			if (paren_depth == 0)
				sel *= PARTIAL_WILDCARD_SEL;
		}
		else if (patt[pos] == '\\')
		{
			/* backslash quotes the next character */
			pos++;
			if (pos >= pattlen)
				break;
			if (paren_depth == 0)
				sel *= FIXED_CHAR_SEL;
		}
		else
		{
			if (paren_depth == 0)
				sel *= FIXED_CHAR_SEL;
		}
	}
	/* Could get sel > 1 if multiple wildcards */
	if (sel > 1.0)
		sel = 1.0;
	return sel;
}

static Selectivity
regex_selectivity(char *patt, bool case_insensitive)
{
	Selectivity		sel;
	int				pattlen = strlen(patt);

	/* If patt doesn't end with $, consider it to have a trailing wildcard */
	if (pattlen > 0 && patt[pattlen-1] == '$' &&
		(pattlen == 1 || patt[pattlen-2] != '\\'))
	{
		/* has trailing $ */
		sel = regex_selectivity_sub(patt, pattlen-1, case_insensitive);
	}
	else
	{
		/* no trailing $ */
		sel = regex_selectivity_sub(patt, pattlen, case_insensitive);
		sel *= FULL_WILDCARD_SEL;
		if (sel > 1.0)
			sel = 1.0;
	}
	return sel;
}

static Selectivity
pattern_selectivity(char *patt, Pattern_Type ptype)
{
	Selectivity result;

	switch (ptype)
	{
		case Pattern_Type_Like:
			result = like_selectivity(patt);
			break;
		case Pattern_Type_Regex:
			result = regex_selectivity(patt, false);
			break;
		case Pattern_Type_Regex_IC:
			result = regex_selectivity(patt, true);
			break;
		default:
			elog(ERROR, "pattern_selectivity: bogus ptype");
			result = 1.0;		/* keep compiler quiet */
			break;
	}
	return result;
}


/*
 * Try to generate a string greater than the given string or any string it is
 * a prefix of.  If successful, return a palloc'd string; else return NULL.
 *
 * To work correctly in non-ASCII locales with weird collation orders,
 * we cannot simply increment "foo" to "fop" --- we have to check whether
 * we actually produced a string greater than the given one.  If not,
 * increment the righthand byte again and repeat.  If we max out the righthand
 * byte, truncate off the last character and start incrementing the next.
 * For example, if "z" were the last character in the sort order, then we
 * could produce "foo" as a string greater than "fonz".
 *
 * This could be rather slow in the worst case, but in most cases we won't
 * have to try more than one or two strings before succeeding.
 *
 * XXX in a sufficiently weird locale, this might produce incorrect results?
 * For example, in German I believe "ss" is treated specially --- if we are
 * given "foos" and return "foot", will this actually be greater than "fooss"?
 */
char *
make_greater_string(const char *str, Oid datatype)
{
	char	   *workstr;
	int			len;

	/*
	 * Make a modifiable copy, which will be our return value if
	 * successful
	 */
	workstr = pstrdup((char *) str);

	while ((len = strlen(workstr)) > 0)
	{
		unsigned char *lastchar = (unsigned char *) (workstr + len - 1);

		/*
		 * Try to generate a larger string by incrementing the last byte.
		 */
		while (*lastchar < (unsigned char) 255)
		{
			(*lastchar)++;
			if (string_lessthan(str, workstr, datatype))
				return workstr; /* Success! */
		}

		/*
		 * Truncate off the last character, which might be more than 1
		 * byte in MULTIBYTE case.
		 */
#ifdef MULTIBYTE
		len = pg_mbcliplen((const unsigned char *) workstr, len, len - 1);
		workstr[len] = '\0';
#else
		*lastchar = '\0';
#endif
	}

	/* Failed... */
	pfree(workstr);
	return NULL;
}

/*
 * Test whether two strings are "<" according to the rules of the given
 * datatype.  We do this the hard way, ie, actually calling the type's
 * "<" operator function, to ensure we get the right result...
 */
static bool
string_lessthan(const char *str1, const char *str2, Oid datatype)
{
	Datum		datum1 = string_to_datum(str1, datatype);
	Datum		datum2 = string_to_datum(str2, datatype);
	bool		result;

	switch (datatype)
	{
		case TEXTOID:
			result = text_lt((text *) datum1, (text *) datum2);
			break;

		case BPCHAROID:
			result = bpcharlt((char *) datum1, (char *) datum2);
			break;

		case VARCHAROID:
			result = varcharlt((char *) datum1, (char *) datum2);
			break;

		case NAMEOID:
			result = namelt((NameData *) datum1, (NameData *) datum2);
			break;

		default:
			elog(ERROR, "string_lessthan: unexpected datatype %lu", datatype);
			result = false;
			break;
	}

	pfree(DatumGetPointer(datum1));
	pfree(DatumGetPointer(datum2));

	return result;
}

/* See if there is a binary op of the given name for the given datatype */
static Oid
find_operator(const char *opname, Oid datatype)
{
	HeapTuple	optup;

	optup = SearchSysCacheTuple(OPERNAME,
								PointerGetDatum(opname),
								ObjectIdGetDatum(datatype),
								ObjectIdGetDatum(datatype),
								CharGetDatum('b'));
	if (!HeapTupleIsValid(optup))
		return InvalidOid;
	return optup->t_data->t_oid;
}

/*
 * Generate a Datum of the appropriate type from a C string.
 * Note that all of the supported types are pass-by-ref, so the
 * returned value should be pfree'd if no longer needed.
 */
static Datum
string_to_datum(const char *str, Oid datatype)
{

	/*
	 * We cheat a little by assuming that textin() will do for bpchar and
	 * varchar constants too...
	 */
	if (datatype == NAMEOID)
		return PointerGetDatum(namein((char *) str));
	else
		return PointerGetDatum(textin((char *) str));
}

/*-------------------------------------------------------------------------
 *
 * Index cost estimation functions
 *
 * genericcostestimate is a general-purpose estimator for use when we
 * don't have any better idea about how to estimate.  Index-type-specific
 * knowledge can be incorporated in the type-specific routines.
 *
 *-------------------------------------------------------------------------
 */

static void
genericcostestimate(Query *root, RelOptInfo *rel,
					IndexOptInfo *index, List *indexQuals,
					Cost *indexStartupCost,
					Cost *indexTotalCost,
					Selectivity *indexSelectivity)
{
	double		numIndexTuples;
	double		numIndexPages;
        double          evalcost = cost_qual_eval(indexQuals);

	/* Estimate the fraction of main-table tuples that will be visited */
	*indexSelectivity = clauselist_selectivity(root, indexQuals,
											   lfirsti(rel->relids));

	/* Estimate the number of index tuples that will be visited */
	numIndexTuples = *indexSelectivity * index->tuples;

	/* Estimate the number of index pages that will be retrieved */
	numIndexPages = *indexSelectivity * index->pages;

	/*
	 * Always estimate at least one tuple and page are touched, even when
	 * indexSelectivity estimate is tiny.
	 */
	if (numIndexTuples < 1.0)
		numIndexTuples = 1.0;
	if (numIndexPages < 1.0)
		numIndexPages = 1.0;

	/*
	 * Compute the index access cost.
	 *
	 * Our generic assumption is that the index pages will be read
	 * sequentially, so they have cost 1.0 each, not random_page_cost.
	 * Also, we charge for evaluation of the indexquals at each index
	 * tuple. All the costs are assumed to be paid incrementally during
	 * the scan.
	 */
        DTRACE_PROBE4(mtpg,indexcost,&numIndexTuples,&numIndexPages,indexSelectivity,&evalcost);
	*indexStartupCost = 0;
	*indexTotalCost = numIndexPages +
		((GetCostInfo()->cpu_index_tuple_cost + evalcost) * numIndexTuples);
}

/*
 * For first cut, just use generic function for all index types.
 */

void
btcostestimate(Query *root, RelOptInfo *rel,
			   IndexOptInfo *index, List *indexQuals,
			   Cost *indexStartupCost,
			   Cost *indexTotalCost,
			   Selectivity *indexSelectivity)
{
	genericcostestimate(root, rel, index, indexQuals,
					 indexStartupCost, indexTotalCost, indexSelectivity);
}

void
rtcostestimate(Query *root, RelOptInfo *rel,
			   IndexOptInfo *index, List *indexQuals,
			   Cost *indexStartupCost,
			   Cost *indexTotalCost,
			   Selectivity *indexSelectivity)
{
	genericcostestimate(root, rel, index, indexQuals,
					 indexStartupCost, indexTotalCost, indexSelectivity);
}

void
hashcostestimate(Query *root, RelOptInfo *rel,
				 IndexOptInfo *index, List *indexQuals,
				 Cost *indexStartupCost,
				 Cost *indexTotalCost,
				 Selectivity *indexSelectivity)
{
	genericcostestimate(root, rel, index, indexQuals,
					 indexStartupCost, indexTotalCost, indexSelectivity);
}

void
gistcostestimate(Query *root, RelOptInfo *rel,
				 IndexOptInfo *index, List *indexQuals,
				 Cost *indexStartupCost,
				 Cost *indexTotalCost,
				 Selectivity *indexSelectivity)
{
	genericcostestimate(root, rel, index, indexQuals,
					 indexStartupCost, indexTotalCost, indexSelectivity);
}
