/*-------------------------------------------------------------------------
 *
 * pqformat.h
 *		Definitions for formatting and parsing frontend/backend messages
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef PQFORMAT_H
#define PQFORMAT_H

#include "lib/stringinfo.h"

#define pq_beginmessage(buf)  initStringInfo(buf)

extern void pq_sendbyte(StringInfo buf, int byt);
extern void pq_sendbytes(StringInfo buf, const char *data, int datalen);
extern void pq_sendcountedtext(StringInfo buf, const char *str, int slen);
extern void pq_sendstring(StringInfo buf, const char *str);
extern void pq_sendint(StringInfo buf, int i, int b);
extern void pq_sendfloat4(StringInfo buf, float f);
extern void pq_endmessage(StringInfo buf);

/* typsend / typreceive helpers (external binary type I/O) */
extern void pq_begintypsend(StringInfo buf);
extern struct varlena *pq_endtypsend(StringInfo buf);
extern void pq_copymsgbytes(StringInfo msg, char *buf, int datalen);
extern unsigned int pq_getmsgint(StringInfo msg, int b);
extern float pq_getmsgfloat4(StringInfo msg);

extern int	pq_puttextmessage(char msgtype, const char *str);

extern int	pq_getint(int *result, int b);
extern int	pq_getstr(StringInfo s);

#endif	 /* PQFORMAT_H */
