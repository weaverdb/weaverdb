/*-------------------------------------------------------------------------
 *
 * xid.c
 *	  POSTGRES transaction identifier code.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *	$Id: xid.c,v 1.1.1.1 2006/08/12 00:20:06 synmscott Exp $
 *
 * OLD COMMENTS
 * XXX WARNING
 *		Much of this file will change when we change our representation
 *		of transaction ids -cim 3/23/90
 *
 * It is time to make the switch from 5 byte to 4 byte transaction ids
 * This file was totally reworked. -mer 5/22/92
 *
 *-------------------------------------------------------------------------
 */


#include "postgres.h"

#include "env/env.h"

#include "access/xact.h"



extern TransactionId NullTransactionId;
extern TransactionId DisabledTransactionId;
extern TransactionId AmiTransactionId;
extern TransactionId FirstTransactionId;

/* XXX name for catalogs */
TransactionId
xidin(char *representation)
{
#ifdef MACOSX
    return (TransactionId)atoi(representation);
#else
    return atoll(representation);
#endif
}

/* XXX name for catalogs */
char *
xidout(TransactionId* transactionId)
{
	/* maximum 32 bit unsigned integer representation takes 10 chars */
	char	   *representation = palloc(64);

	snprintf(representation, 64, "%lu", *transactionId);

	return representation;

}

/* ----------------------------------------------------------------
 *		xideq
 * ----------------------------------------------------------------
 */

/*
 *		xideq			- returns 1, iff xid1 == xid2
 *								  0  else;
 */
bool
xideq(TransactionId* xid1, TransactionId* xid2)
{
	return (bool) (*xid1 == *xid2);
}



/* ----------------------------------------------------------------
 *		TransactionIdAdd
 * ----------------------------------------------------------------
 */
void
TransactionIdAdd(TransactionId *xid, int value)
{
	*xid += ((TransactionId)0ULL | value);
	return;
}

bool xidint8_equals(TransactionId* xid,int64 comp) {
	return *xid == comp;
}

bool xidint8_lt(TransactionId* xid,int64 comp) {
	return *xid < comp;
}

PG_EXTERN bool xidint8_gt(TransactionId* xid,int64 comp) {
	return *xid > comp;
}

PG_EXTERN bool xidint8_lteq(TransactionId* xid,int64 comp) {
	return *xid <= comp;
}

PG_EXTERN bool xidint8_gteq(TransactionId* xid,int64 comp) {
	return *xid >= comp;
}

PG_EXTERN bool xidint8_noteq(TransactionId* xid,int64 comp) {
	return *xid != comp;
}

PG_EXTERN bool xidint4_equals(TransactionId* xid,int32 comp) {
	return *xid == comp;
}

PG_EXTERN bool xidint4_lt(TransactionId* xid,int32 comp) {
	return *xid < comp;
}

PG_EXTERN bool xidint4_gt(TransactionId* xid,int32 comp) {
	return *xid > comp;
}

PG_EXTERN bool xidint4_lteq(TransactionId* xid,int32 comp) {
	return *xid <= comp;
}

PG_EXTERN bool xidint4_gteq(TransactionId* xid,int32 comp) {
	return *xid >= comp;
}

PG_EXTERN bool xidint4_noteq(TransactionId* xid,int32 comp) {
	return *xid != comp;
}
