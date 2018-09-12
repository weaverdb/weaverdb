/*
 * psql - the PostgreSQL interactive terminal
 *
 * Copyright 2000 by PostgreSQL Global Development Group
 *
 * $Header: /cvs/weaver/mtpgsql/src/bin/psql/copy.h,v 1.1.1.1 2006/08/12 00:11:49 synmscott Exp $
 */
#ifndef COPY_H
#define COPY_H

#include "libpq-fe.h"

extern bool copy_in_state;

/* handler for \copy */
bool		do_copy(const char *args);

/* lower level processors for copy in/out streams */

bool		handleCopyOut(PGconn *conn, FILE *copystream);
bool		handleCopyIn(PGconn *conn, FILE *copystream, const char *prompt);

#endif
