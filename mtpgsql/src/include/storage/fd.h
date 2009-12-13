/*-------------------------------------------------------------------------
 *
 * fd.h
 *	  Virtual file descriptor definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: fd.h,v 1.2 2007/01/13 20:38:45 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

/*
 * calls:
 *
 *	File {Close, Read, Write, Seek, Tell, MarkDirty, Sync}
 *	{File Name Open, Allocate, Free} File
 *
 * These are NOT JUST RENAMINGS OF THE UNIX ROUTINES.
 * Use them for all file activity...
 *
 *	File fd;
 *	fd = FilePathOpenFile("foo", O_RDONLY);
 *
 *	AllocateFile();
 *	FreeFile();
 *
 * Use AllocateFile, not fopen, if you need a stdio file (FILE*); then
 * use FreeFile, not fclose, to close it.  AVOID using stdio for files
 * that you intend to hold open for any length of time, since there is
 * no way for them to share kernel file descriptors with other files.
 */
#ifndef FD_H
#define FD_H

/*
 * FileSeek uses the standard UNIX lseek(2) flags.
 */

typedef char *FileName;

typedef int File;

/*
 * prototypes for functions in fd.c
 */

/* Operations on virtual Files --- equivalent to Unix kernel file ops */

#ifdef __cplusplus
extern "C" {
#endif


PG_EXTERN File FileNameOpenFile(FileName fileName, int fileFlags, int fileMode);
PG_EXTERN File PathNameOpenFile(FileName fileName, int fileFlags, int fileMode);

PG_EXTERN int FileOptimize(File file);
PG_EXTERN int FileNormalize(File file);

PG_EXTERN File OpenTemporaryFile(void);
PG_EXTERN char* FileName(File file);
PG_EXTERN void FileClose(File file);
PG_EXTERN void FileUnlink(File file);
PG_EXTERN int	FileRead(File file, char *buffer, int amount);
PG_EXTERN int	FileWrite(File file, char *buffer, int amount);
PG_EXTERN long FileSeek(File file, long offset, int whence);
PG_EXTERN int	FileTruncate(File file, long offset);
PG_EXTERN int   FileBaseSync(File file, long offset);   /*  sync the OS open file pointers with a DB change */
PG_EXTERN int	FileSync(File file);
PG_EXTERN int	FilePin(File file,int key);
PG_EXTERN int	FileUnpin(File file,int key);
PG_EXTERN void FileMarkDirty(File file);

/*  added to explicitly innitialize file system, before done in allocateVFD */
PG_EXTERN void InitVirtualFileSystem();
PG_EXTERN void ShutdownVirtualFileSystem();

/* Operations that allow use of regular stdio --- USE WITH CAUTION */
PG_EXTERN FILE *AllocateFile(char *name, char *mode);
PG_EXTERN void FreeFile(FILE *);

/* Miscellaneous support routines */
PG_EXTERN bool ReleaseDataFile(void);
PG_EXTERN void AtEOXact_Files(void);
PG_EXTERN int	pg_fsync(int fd);

#ifdef __cplusplus
}
#endif
#endif	 /* FD_H */
