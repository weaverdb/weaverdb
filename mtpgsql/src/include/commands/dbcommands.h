/*-------------------------------------------------------------------------
 *
 * dbcommands.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef DBCOMMANDS_H
#define DBCOMMANDS_H

PG_EXTERN void createdb(const char *dbname, const char *dbpath, int encoding);
PG_EXTERN void createschema(const char *schema,int encoding);
PG_EXTERN void dropdb(const char *dbname);
PG_EXTERN void dropschema(const char *schema);

#endif	 /* DBCOMMANDS_H */
