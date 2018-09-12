/*-------------------------------------------------------------------------
 *
 *   FILE
 *	pgtransdb.cpp
 *
 *   DESCRIPTION
 *      implementation of the PgTransaction class.
 *   PgConnection encapsulates a transaction querying to backend
 *
 * Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/interfaces/libpq++/pgtransdb.cc,v 1.1.1.1 2006/08/12 00:24:30 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
 
#include "pgtransdb.h"

// ****************************************************************
//
// PgTransaction Implementation
//
// ****************************************************************
// Make a connection to the specified database with default environment
// See PQconnectdb() for conninfo usage. 
PgTransaction::PgTransaction(const char* conninfo)
   : PgDatabase(conninfo)
{
	BeginTransaction();
}

// Destructor: End the transaction block
PgTransaction::~PgTransaction()
{
	EndTransaction();
}

// Begin the transaction block
ExecStatusType PgTransaction::BeginTransaction()
{
	return Exec("BEGIN");
} // End BeginTransaction()

// Begin the transaction block
ExecStatusType PgTransaction::EndTransaction()
{
	return Exec("END");
} // End EndTransaction()
