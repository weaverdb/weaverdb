/*-------------------------------------------------------------------------
 *
 * scansup.h
 *	  scanner support routines.  used by both the bootstrap lexer
 * as well as the normal lexer
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: scansup.h,v 1.1.1.1 2006/08/12 00:22:22 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#ifndef SCANSUP_H
#define SCANSUP_H

PG_EXTERN char *scanstr(char *s);

#endif	 /* SCANSUP_H */
