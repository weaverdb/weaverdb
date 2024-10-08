/*-------------------------------------------------------------------------
 *
 * bool.c
 *	  Functions for the built-in type "bool".
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

#include "utils/builtins.h"

/*****************************************************************************
 *	 USER I/O ROUTINES														 *
 *****************************************************************************/

/*
 *		boolin			- converts "t" or "f" to 1 or 0
 *
 * Check explicitly for "true/false" and TRUE/FALSE, 1/0, YES/NO.
 * Reject other values. - thomas 1997-10-05
 *
 * In the switch statement, check the most-used possibilities first.
 */
bool
boolin(char *b)
{
	switch (*b)
	{
			case 't':
			case 'T':
			if (strncasecmp(b, "true", strlen(b)) == 0)
				return TRUE;
			break;

		case 'f':
		case 'F':
			if (strncasecmp(b, "false", strlen(b)) == 0)
				return FALSE;
			break;

		case 'y':
		case 'Y':
			if (strncasecmp(b, "yes", strlen(b)) == 0)
				return TRUE;
			break;

		case '1':
			if (strncasecmp(b, "1", strlen(b)) == 0)
				return TRUE;
			break;

		case 'n':
		case 'N':
			if (strncasecmp(b, "no", strlen(b)) == 0)
				return FALSE;
			break;

		case '0':
			if (strncasecmp(b, "0", strlen(b)) == 0)
				return FALSE;
			break;

		default:
			break;
	}

	elog(ERROR, "Bad boolean external representation '%s'", b);
	/* not reached */
	return FALSE;
}	/* boolin() */

/*
 *		boolout			- converts 1 or 0 to "t" or "f"
 */
char *
boolout(bool b)
{
	char	   *result = (char *) palloc(2);

	*result = (b) ? 't' : 'f';
	result[1] = '\0';
	return result;
}	/* boolout() */


/*****************************************************************************
 *	 PUBLIC ROUTINES														 *
 *****************************************************************************/

bool
booleq(bool arg1, bool arg2)
{
	return arg1 == arg2;
}

bool
boolne(bool arg1, bool arg2)
{
	return arg1 != arg2;
}

bool
boollt(bool arg1, bool arg2)
{
	return arg1 < arg2;
}

bool
boolgt(bool arg1, bool arg2)
{
	return arg1 > arg2;
}

bool
boolle(bool arg1, bool arg2)
{
	return arg1 <= arg2;
}

bool
boolge(bool arg1, bool arg2)
{
	return arg1 >= arg2;
}

bool
istrue(bool arg1)
{
	return arg1 == TRUE;
}	/* istrue() */

bool
isfalse(bool arg1)
{
	return arg1 != TRUE;
}	/* isfalse() */
