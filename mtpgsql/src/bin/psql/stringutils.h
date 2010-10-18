/*
 * psql - the PostgreSQL interactive terminal
 *
 * Copyright 2000 by PostgreSQL Global Development Group
 *
 * $Header: /cvs/weaver/mtpgsql/src/bin/psql/stringutils.h,v 1.1.1.1 2006/08/12 00:11:52 synmscott Exp $
 */
#ifndef STRINGUTILS_H
#define STRINGUTILS_H

/* The cooler version of strtok() which knows about quotes and doesn't
 * overwrite your input */
extern char *strtokx(const char *s,
		const char *delim,
		const char *quote,
		int escape,
		char *was_quoted,
		unsigned int *token_pos,
		int encoding);

#endif	 /* STRINGUTILS_H */
