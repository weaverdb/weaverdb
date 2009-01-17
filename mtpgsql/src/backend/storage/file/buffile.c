/*-------------------------------------------------------------------------
 *
 * buffile.c
 *	  Management of large buffered files, primarily temporary files.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/storage/file/buffile.c,v 1.1.1.1 2006/08/12 00:21:26 synmscott Exp $
 *
 * NOTES:
 *
 * BufFiles provide a very incomplete emulation of stdio atop virtual Files
 * (as managed by fd.c).  Currently, we only support the buffered-I/O
 * aspect of stdio: a read or write of the low-level File occurs only
 * when the buffer is filled or emptied.  This is an even bigger win
 * for virtual Files than for ordinary kernel files, since reducing the
 * frequency with which a virtual File is touched reduces "thrashing"
 * of opening/closing file descriptors.
 *
 * Note that BufFile structs are allocated with palloc(), and therefore
 * will go away automatically at transaction end.  If the underlying
 * virtual File is made with OpenTemporaryFile, then all resources for
 * the file are certain to be cleaned up even if processing is aborted
 * by elog(ERROR).	To avoid confusion, the caller should take care that
 * all calls for a single BufFile are made in the same palloc context.
 *
 * BufFile also supports temporary files that exceed the OS file size limit
 * (by opening multiple fd.c temporary files).	This is an essential feature
 * for sorts and hashjoins on large amounts of data.
 *-------------------------------------------------------------------------
 */

#include <errno.h>

#include "postgres.h"

#include "storage/buffile.h"

/*
 * The maximum safe file size is presumed to be RELSEG_SIZE * BLCKSZ.
 * Note we adhere to this limit whether or not LET_OS_MANAGE_FILESIZE
 * is defined, although md.c ignores it when that symbol is defined.
 */
#define MAX_PHYSICAL_FILESIZE  (RELSEG_SIZE * BLCKSZ)

/*
 * This data structure represents a buffered file that consists of one or
 * more physical files (each accessed through a virtual file descriptor
 * managed by fd.c).
 */
struct BufFile
{
	File	   file;			/* palloc'd array with numFiles entries */
	long	   offset;		/* palloc'd array with numFiles entries */

	/*
	 * offsets[i] is the current seek position of files[i].  We use this
	 * to avoid making redundant FileSeek calls.
	 */

	bool		isTemp;			/* can only add files if this is TRUE */
	bool		dirty;			/* does buffer need to be written? */

	/*
	 * "current pos" is position of start of buffer within the logical
	 * file. Position as seen by user of BufFile is (curFile, curOffset +
	 * pos).
	 */
	int			curOffset;		/* offset part of current pos */
	int			pos;			/* next read/write position in buffer */
	int			nbytes;			/* total # of valid bytes in buffer */
	char		buffer[BLCKSZ];
};

static BufFile *makeBufFile(File firstfile);
static void extendBufFile(BufFile *file);
static void BufFileLoadBuffer(BufFile *file);
static void BufFileDumpBuffer(BufFile *file);
static int	BufFileFlush(BufFile *file);


/*
 * Create a BufFile given the first underlying physical file.
 * NOTE: caller must set isTemp true if appropriate.
 */
static BufFile *
makeBufFile(File firstfile)
{
	BufFile    *file = (BufFile *) palloc(sizeof(BufFile));

	file->file = firstfile;
	file->offset = 0L;
	file->isTemp = false;
	file->dirty = false;
	file->curOffset = 0L;
	file->pos = 0;
	file->nbytes = 0;

	return file;
}

/*
 * Create a BufFile for a new temporary file (which will expand to become
 * multiple temporary files if more than MAX_PHYSICAL_FILESIZE bytes are
 * written to it).
 */
BufFile    *
BufFileCreateTemp(void)
{
	BufFile    *file;
	File		pfile;

	pfile = OpenTemporaryFile();
	Assert(pfile >= 0);

	file = makeBufFile(pfile);
	file->isTemp = true;

	return file;
}

/*
 * Create a BufFile and attach it to an already-opened virtual File.
 *
 * This is comparable to fdopen() in stdio.  This is the only way at present
 * to attach a BufFile to a non-temporary file.  Note that BufFiles created
 * in this way CANNOT be expanded into multiple files.
 */
BufFile    *
BufFileCreate(File file)
{
	return makeBufFile(file);
}

/*
 * Close a BufFile
 *
 * Like fclose(), this also implicitly FileCloses the underlying File.
 */
void
BufFileClose(BufFile *file)
{
	int			i;

	/* flush any unwritten data */
	BufFileFlush(file);
	FileClose(file->file);
	/* release the buffer space */
	pfree(file);
}

/*
 * BufFileLoadBuffer
 *
 * Load some data into buffer, if possible, starting from curOffset.
 * At call, must have dirty = false, pos and nbytes = 0.
 * On exit, nbytes is number of bytes loaded.
 */
static void
BufFileLoadBuffer(BufFile *file)
{
	File		thisfile;

	/*
	 * May need to reposition physical file.
	 */
	thisfile = file->file;
	if (file->curOffset != file->offset)
	{
		if (FileSeek(thisfile, file->curOffset, SEEK_SET) != file->curOffset)
			return;				/* seek failed, read nothing */
		file->offset = file->curOffset;
	}

	/*
	 * Read whatever we can get, up to a full bufferload.
	 */
	file->nbytes = FileRead(thisfile, file->buffer, sizeof(file->buffer));
	if (file->nbytes < 0)
		file->nbytes = 0;
	file->offset += file->nbytes;
	/* we choose not to advance curOffset here */
}

/*
 * BufFileDumpBuffer
 *
 * Dump buffer contents starting at curOffset.
 * At call, should have dirty = true, nbytes > 0.
 * On exit, dirty is cleared if successful write, and curOffset is advanced.
 */
static void
BufFileDumpBuffer(BufFile *file)
{
	int			wpos = 0;
	int			bytestowrite;
	File		thisfile;

	/*
	 * Unlike BufFileLoadBuffer, we must dump the whole buffer even if it
	 * crosses a component-file boundary; so we need a loop.
	 */
	while (wpos < file->nbytes)
	{
		/*
		 * Enforce per-file size limit only for temp files, else just try
		 * to write as much as asked...
		 */
		bytestowrite = file->nbytes - wpos;

		/*
		 * May need to reposition physical file.
		 */
		thisfile = file->file;
		if (file->curOffset != file->offset)
		{
			if (FileSeek(thisfile, file->curOffset, SEEK_SET) != file->curOffset)
				return;			/* seek failed, give up */
			file->offset = file->curOffset;
		}
		bytestowrite = FileWrite(thisfile, file->buffer, bytestowrite);
		if (bytestowrite <= 0)
			return;				/* failed to write */
		file->offset += bytestowrite;
		file->curOffset += bytestowrite;
		wpos += bytestowrite;
	}
	file->dirty = false;

	/*
	 * At this point, curOffset has been advanced to the end of the
	 * buffer, ie, its original value + nbytes.  We need to make it point
	 * to the logical file position, ie, original value + pos, in case
	 * that is less (as could happen due to a small backwards seek in a
	 * dirty buffer!)
	 */
	file->curOffset -= (file->nbytes - file->pos);

	/*
	 * Now we can set the buffer empty without changing the logical
	 * position
	 */
	file->pos = 0;
	file->nbytes = 0;
}

/*
 * BufFileRead
 *
 * Like fread() except we assume 1-byte element size.
 */
size_t
BufFileRead(BufFile *file, void *ptr, size_t size)
{
	size_t		nread = 0;
	size_t		nthistime;

	if (file->dirty)
	{
		if (BufFileFlush(file) != 0)
			return 0;			/* could not flush... */
		Assert(!file->dirty);
	}

	while (size > 0)
	{
		if (file->pos >= file->nbytes)
		{
			/* Try to load more data into buffer. */
			file->curOffset += file->pos;
			file->pos = 0;
			file->nbytes = 0;
			BufFileLoadBuffer(file);
			if (file->nbytes <= 0)
				break;			/* no more data available */
		}

		nthistime = file->nbytes - file->pos;
		if (nthistime > size)
			nthistime = size;
		Assert(nthistime > 0);

		memcpy(ptr, file->buffer + file->pos, nthistime);

		file->pos += nthistime;
		ptr = (void *) ((char *) ptr + nthistime);
		size -= nthistime;
		nread += nthistime;
	}

	return nread;
}

/*
 * BufFileWrite
 *
 * Like fwrite() except we assume 1-byte element size.
 */
size_t
BufFileWrite(BufFile *file, void *ptr, size_t size)
{
	size_t		nwritten = 0;
	size_t		nthistime;

	while (size > 0)
	{
		if (file->pos >= BLCKSZ)
		{
			/* Buffer full, dump it out */
			if (file->dirty)
			{
				BufFileDumpBuffer(file);
				if (file->dirty)
					break;		/* I/O error */
			}
			else
			{
				/* Hmm, went directly from reading to writing? */
				file->curOffset += file->pos;
				file->pos = 0;
				file->nbytes = 0;
			}
		}

		nthistime = BLCKSZ - file->pos;
		if (nthistime > size)
			nthistime = size;
		Assert(nthistime > 0);

		memcpy(file->buffer + file->pos, ptr, nthistime);

		file->dirty = true;
		file->pos += nthistime;
		if (file->nbytes < file->pos)
			file->nbytes = file->pos;
		ptr = (void *) ((char *) ptr + nthistime);
		size -= nthistime;
		nwritten += nthistime;
	}

	return nwritten;
}

/*
 * BufFileFlush
 *
 * Like fflush()
 */
static int
BufFileFlush(BufFile *file)
{
	if (file->dirty)
	{
		BufFileDumpBuffer(file);
		if (file->dirty)
			return EOF;
	}

	return 0;
}

/*
 * BufFileSeek
 *
 * Like fseek(), except that target position needs two values in order to
 * work when logical filesize exceeds maximum value representable by long.
 * We do not support relative seeks across more than LONG_MAX, however.
 *
 * Result is 0 if OK, EOF if not.  Logical position is not moved if an
 * impossible seek is attempted.
 */
int
BufFileSeek(BufFile *file, long offset, int whence)
{
	long		newOffset;

	switch (whence)
	{
		case SEEK_SET:
			newOffset = offset;
			break;
		case SEEK_CUR:

			/*
			 * Relative seek considers only the signed offset, ignoring
			 * fileno. Note that large offsets (> 1 gig) risk overflow in
			 * this add...
			 */
			newOffset = (file->curOffset + file->pos) + offset;
			break;
		default:
			elog(ERROR, "BufFileSeek: invalid whence: %d", whence);
			return EOF;
	}
	if (
		newOffset >= file->curOffset &&
		newOffset <= file->curOffset + file->nbytes)
	{

		/*
		 * Seek is to a point within existing buffer; we can just adjust
		 * pos-within-buffer, without flushing buffer.	Note this is OK
		 * whether reading or writing, but buffer remains dirty if we were
		 * writing.
		 */
		file->pos = (int) (newOffset - file->curOffset);
		return 0;
	}
	/* Otherwise, must reposition buffer, so flush any dirty data */
	if (BufFileFlush(file) != 0)
		return EOF;

	/* Seek is OK! */
	file->curOffset = newOffset;
	file->pos = 0;
	file->nbytes = 0;
	return 0;
}

void
BufFileTell(BufFile *file, long *offset)
{
	*offset = file->curOffset + file->pos;
}

/*
 * BufFileSeekBlock --- block-oriented seek
 *
 * Performs absolute seek to the start of the n'th BLCKSZ-sized block of
 * the file.  Note that users of this interface will fail if their files
 * exceed BLCKSZ * LONG_MAX bytes, but that is quite a lot; we don't work
 * with tables bigger than that, either...
 *
 * Result is 0 if OK, EOF if not.  Logical position is not moved if an
 * impossible seek is attempted.
 */
int
BufFileSeekBlock(BufFile *file, long blknum)
{
	return BufFileSeek(file,
					   blknum * BLCKSZ,
					   SEEK_SET);
}

/*
 * BufFileTellBlock --- block-oriented tell
 *
 * Any fractional part of a block in the current seek position is ignored.
 */
long
BufFileTellBlock(BufFile *file)
{
	long		blknum;

	blknum = (file->curOffset + file->pos) / BLCKSZ;
	return blknum;
}

