/*-------------------------------------------------------------------------
 *
 * buffile.h
 *	  Management of large buffered files, primarily temporary files.
 *
 * The BufFile routines provide a partial replacement for stdio atop
 * virtual file descriptors managed by fd.c.  Currently they only support
 * buffered access to a virtual file, without any of stdio's formatting
 * features.  That's enough for immediate needs, but the set of facilities
 * could be expanded if necessary.
 *
 * BufFile also supports working with temporary files that exceed the OS
 * file size limit and/or the largest offset representable in an int.
 * It might be better to split that out as a separately accessible module,
 * but currently we have no need for oversize temp files without buffered
 * access.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */

#ifndef BUFFILE_H
#define BUFFILE_H

#include "storage/fd.h"

/* BufFile is an opaque type whose details are not known outside buffile.c. */

typedef struct BufFile BufFile;

/*
 * prototypes for functions in buffile.c
 */

PG_EXTERN BufFile *BufFileCreateTemp(void);
PG_EXTERN BufFile *BufFileCreate(File file);
PG_EXTERN void BufFileClose(BufFile *file);
PG_EXTERN size_t BufFileRead(BufFile *file, void *ptr, size_t size);
PG_EXTERN size_t BufFileWrite(BufFile *file, void *ptr, size_t size);
PG_EXTERN int	BufFileSeek(BufFile *file, long offset, int whence);
PG_EXTERN void BufFileTell(BufFile *file, long *offset);
PG_EXTERN int	BufFileSeekBlock(BufFile *file, long blknum);
PG_EXTERN long BufFileTellBlock(BufFile *file);

#endif	 /* BUFFILE_H */
