/*
 * psql - the PostgreSQL interactive terminal
 *
 * Copyright 2000 by PostgreSQL Global Development Group
 *
 * $Header: /cvs/weaver/mtpgsql/src/bin/psql/large_obj.h,v 1.1.1.1 2006/08/12 00:11:50 synmscott Exp $
 */
#ifndef LARGE_OBJ_H
#define LARGE_OBJ_H

bool		do_lo_export(const char *loid_arg, const char *filename_arg);
bool		do_lo_import(const char *filename_arg, const char *comment_arg);
bool		do_lo_unlink(const char *loid_arg);
bool		do_lo_list(void);

#endif	 /* LARGE_OBJ_H */
