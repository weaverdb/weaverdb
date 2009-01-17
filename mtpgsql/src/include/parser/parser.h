/*-------------------------------------------------------------------------
 *
 * parser.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: parser.h,v 1.1.1.1 2006/08/12 00:22:22 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef PARSER_H
#define PARSER_H

#include "parser/parse_node.h"

PG_EXTERN List *parser(char *str, Oid *typev, char** argnames, int nargs);

#endif	 /* PARSER_H */
