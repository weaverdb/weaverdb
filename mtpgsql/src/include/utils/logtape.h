/*-------------------------------------------------------------------------
 *
 * logtape.h
 *	  Management of "logical tapes" within temporary files.
 *
 * See logtape.c for explanations.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: logtape.h,v 1.1.1.1 2006/08/12 00:22:27 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#ifndef LOGTAPE_H
#define LOGTAPE_H

/* LogicalTapeSet is an opaque type whose details are not known outside logtape.c. */

typedef struct LogicalTapeSet LogicalTapeSet;

/*
 * prototypes for functions in logtape.c
 */

PG_EXTERN LogicalTapeSet *LogicalTapeSetCreate(int ntapes);
PG_EXTERN void LogicalTapeSetClose(LogicalTapeSet *lts);
PG_EXTERN size_t LogicalTapeRead(LogicalTapeSet *lts, int tapenum,
				void *ptr, size_t size);
PG_EXTERN void LogicalTapeWrite(LogicalTapeSet *lts, int tapenum,
				 void *ptr, size_t size);
PG_EXTERN void LogicalTapeRewind(LogicalTapeSet *lts, int tapenum, bool forWrite);
PG_EXTERN void LogicalTapeFreeze(LogicalTapeSet *lts, int tapenum);
PG_EXTERN bool LogicalTapeBackspace(LogicalTapeSet *lts, int tapenum,
					 size_t size);
PG_EXTERN bool LogicalTapeSeek(LogicalTapeSet *lts, int tapenum,
				long blocknum, int offset);
PG_EXTERN void LogicalTapeTell(LogicalTapeSet *lts, int tapenum,
				long *blocknum, int *offset);

#endif	 /* LOGTAPE_H */
