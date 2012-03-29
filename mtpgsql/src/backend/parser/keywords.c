/*-------------------------------------------------------------------------
 *
 * keywords.c
 *	  lexical token lookup for reserved words in postgres SQL
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/parser/keywords.c,v 1.2 2006/08/15 01:37:17 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#include <ctype.h>

#include "postgres.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "parse.h"
#include "parser/keywords.h"

/*
 * List of (keyword-name, keyword-token-value) pairs.
 *
 * !!WARNING!!: This list must be sorted, because binary
 *		 search is used to locate entries.
 */
static ScanKeyword ScanKeywords[] = {
	/* name, value */
	{"abort", ABORT_TRANS},
	{"absolute", ABSOLUTE},
	{"access", ACCESS},
	{"action", ACTION},
	{"add", ADD},
	{"after", AFTER},
	{"aggregate", AGGREGATE},
	{"all", ALL},
	{"alter", ALTER},
	{"analyze", ANALYZE},
	{"and", AND},
	{"any", ANY},
/*  added for type non-specific slots */        
        {"array", S_ARRAY},
	{"as", AS},
	{"asc", ASC},
	{"backward", BACKWARD},
	{"before", BEFORE},
	{"begin", BEGIN_TRANS},
	{"between", BETWEEN},
	{"binary", BINARY},
	{"bit", BIT},
	{"both", BOTH},
	{"by", BY},
	{"cache", CACHE},
	{"cascade", CASCADE},
	{"case", CASE},
	{"cast", CAST},
	{"char", CHAR},
	{"character", CHARACTER},
	{"check", CHECK},
	{"close", CLOSE},
	{"cluster", CLUSTER},
	{"coalesce", COALESCE},
	{"collate", COLLATE},
	{"column", COLUMN},
	{"comment", COMMENT},
	{"commit", COMMIT},
	{"committed", COMMITTED},
	{"compact", COMPACT},
	{"constraint", CONSTRAINT},
	{"constraints", CONSTRAINTS},
	{"copy", COPY},
	{"create", CREATE},
	{"createdb", CREATEDB},
	{"createuser", CREATEUSER},
	{"cross", CROSS},
	{"current_date", CURRENT_DATE},
	{"current_time", CURRENT_TIME},
	{"current_timestamp", CURRENT_TIMESTAMP},
	{"current_user", CURRENT_USER},
	{"cursor", CURSOR},
	{"cycle", CYCLE},
	{"database", DATABASE},
	{"day", DAY_P},
	{"dec", DEC},
	{"decimal", DECIMAL},
	{"declare", DECLARE},
	{"default", DEFAULT},
	{"deferrable", DEFERRABLE},
	{"deferred", DEFERRED},	
	{"defrag", DEFRAG},
	{"delete", DELETE},
	{"delimiters", DELIMITERS},
	{"desc", DESC},
	{"distinct", DISTINCT},
	{"do", DO},
	{"double", DOUBLE},
	{"drop", DROP},
	{"each", EACH},
	{"else", ELSE},
	{"encoding", ENCODING},
	{"end", END_TRANS},
	{"except", EXCEPT},
	{"exclusive", EXCLUSIVE},
	{"execute", EXECUTE},
	{"exists", EXISTS},
	{"explain", EXPLAIN},
	{"extend", EXTEND},
	{"extent", EXTENT},
	{"extract", EXTRACT},
	{"false", FALSE_P},
	{"fetch", FETCH},
	{"fixflags", FIXFLAGS},
	{"float", FLOAT},
	{"for", FOR},
	{"force", FORCE},
	{"foreign", FOREIGN},
	{"forward", FORWARD},
	{"freespace", FREESPACE},	
	{"from", FROM},
	{"full", FULL},
	{"function", FUNCTION},
	{"global", GLOBAL},
#ifdef USEACL
	{"grant", GRANT},
#endif
	{"group", GROUP},
	{"handler", HANDLER},
	{"having", HAVING},
	{"hour", HOUR_P},
	{"immediate", IMMEDIATE},
	{"in", IN},
	{"increment", INCREMENT},
	{"index", INDEX},
	{"inherits", INHERITS},
	{"initially", INITIALLY},
	{"inner", INNER_P},
	{"insensitive", INSENSITIVE},
	{"insert", INSERT},
	{"instanceof",INSTANCEOF},
	{"instead", INSTEAD},
	{"intersect", INTERSECT},
	{"interval", INTERVAL},
	{"into", INTO},
	{"is", IS},
	{"isnull", ISNULL},
	{"isolation", ISOLATION},
	{"join", JOIN},
	{"key", KEY},
	{"lancompiler", LANCOMPILER},
	{"language", LANGUAGE},
	{"leading", LEADING},
	{"left", LEFT},
	{"level", LEVEL},
	{"like", LIKE},
	{"limit", LIMIT},
	{"listen", LISTEN},
	{"load", LOAD},
	{"local", LOCAL},
	{"location", LOCATION},
	{"lock", LOCK_P},
	{"match", MATCH},
	{"maxvalue", MAXVALUE},
	{"memory", MEMORY},
	{"minute", MINUTE_P},
	{"minvalue", MINVALUE},
	{"mode", MODE},
	{"month", MONTH_P},
	{"move", MOVE},
	{"names", NAMES},
	{"national", NATIONAL},
	{"natural", NATURAL},
	{"nchar", NCHAR},
	{"new", NEW},
	{"next", NEXT},
/* added for non-specific slot */
        {"nil",S_NIL},
	{"no", NO},
	{"nocreatedb", NOCREATEDB},
	{"nocreateuser", NOCREATEUSER},
	{"none", NONE},
	{"not", NOT},
	{"nothing", NOTHING},
	{"notify", NOTIFY},
	{"notnull", NOTNULL},
	{"nowait", NOWAIT},
	{"null", NULL_P},
	{"nullif", NULLIF},
	{"numeric", NUMERIC},
	{"of", OF},
	{"offset", OFFSET},
	{"oids", OIDS},
	{"old", CURRENT},
	{"on", ON},
	{"only", ONLY},
	{"operator", OPERATOR},
	{"option", OPTION},
	{"or", OR},
	{"order", ORDER},
	{"outer", OUTER_P},
	{"overlaps", OVERLAPS},
	{"partial", PARTIAL},
	{"password", PASSWORD},
/* added for non-specific slot */
        {"pattern",S_PATTERN},
	{"pendant", PENDANT},
	{"position", POSITION},
	{"precision", PRECISION},
	{"primary", PRIMARY},
	{"prior", PRIOR},
#ifdef USEACL
	{"privileges", PRIVILEGES},
#endif
	{"procedural", PROCEDURAL},
	{"procedure", PROCEDURE},
	{"prune", PRUNE},
	{"public", PUBLIC},
	{"put", PUT},
	{"read", READ},
	{"references", REFERENCES},
	{"reindex", REINDEX},
	{"relative", RELATIVE},
	{"rename", RENAME},
	{"report", REPORT},
	{"reset", RESET},
        {"respan",RESPAN},
	{"restrict", RESTRICT},
	{"returns", RETURNS},
#ifdef USEACL
	{"revoke", REVOKE},
#endif
	{"right", RIGHT},
	{"rollback", ROLLBACK},
	{"row", ROW},
	{"rule", RULE},
/*  added by myron scott */
	{"scan", SCAN},
	{"schema", SCHEMA},
/*  end add   */
	{"scroll", SCROLL},
	{"second", SECOND_P},
	{"select", SELECT},
	{"sequence", SEQUENCE},
	{"serial", SERIAL},
	{"serializable", SERIALIZABLE},
	{"session_user", SESSION_USER},
	{"set", SET},
	{"setof", SETOF},
	{"share", SHARE},
	{"show", SHOW},
        {"snapshot",SNAPSHOT},
	{"some", SOME},
	{"start", START},
	{"statement", STATEMENT},
	{"stats", STATS},
	{"stdin", STDIN},
	{"stdout", STDOUT},
	{"substring", SUBSTRING},
	{"sysid", SYSID},
	{"system", SYSTEM},        
	{"table", TABLE},
	{"temp", TEMP},
	{"temporary", TEMPORARY},
	{"then", THEN},
	{"time", TIME},
	{"timestamp", TIMESTAMP},
	{"timezone_hour", TIMEZONE_HOUR},
	{"timezone_minute", TIMEZONE_MINUTE},
	{"to", TO},
	{"trailing", TRAILING},
	{"transaction", TRANSACTION},
	{"trigger", TRIGGER},
	{"trim", TRIM},
	{"true", TRUE_P},
	{"truncate", TRUNCATE},
	{"trusted", TRUSTED},
	{"type", TYPE_P},
	{"union", UNION},
	{"unique", UNIQUE},
	{"unlisten", UNLISTEN},
	{"until", UNTIL},
	{"update", UPDATE},
	{"user", USER},
	{"using", USING},
	{"vacuum", VACUUM},
	{"valid", VALID},
	{"values", VALUES},
	{"varchar", VARCHAR},
	{"varying", VARYING},
	{"verbose", VERBOSE},
	{"version", VERSION},
	{"view", VIEW},
	{"when", WHEN},
	{"where", WHERE},
	{"with", WITH},
	{"work", WORK},
	{"year", YEAR_P},
	{"zone", ZONE},
};

ScanKeyword *
ScanKeywordLookup(char *text)
{
	ScanKeyword *low = &ScanKeywords[0];
	ScanKeyword *high = endof(ScanKeywords) - 1;
	ScanKeyword *middle;
	int			difference;

	while (low <= high)
	{
		middle = low + (high - low) / 2;
		difference = strcmp(middle->name, text);
		if (difference == 0)
			return middle;
		else if (difference < 0)
			low = middle + 1;
		else
			high = middle - 1;
	}

	return NULL;
}
