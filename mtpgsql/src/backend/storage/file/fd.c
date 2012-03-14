/*------------------------------------------------------------------------
 *
 * fd.c
 *	  Virtual file descriptor code.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/storage/file/fd.c,v 1.3 2007/04/22 23:16:49 synmscott Exp $
 *
 * NOTES:
 *
 *-------------------------------------------------------------------------
 */
#include <sys/types.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>

#include "postgres.h"
#include "env/env.h"
#include "env/connectionutil.h"
#include "env/dbwriter.h"
#include "env/poolsweep.h"
#include "miscadmin.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "utils/hsearch.h"

/*
 * Problem: Postgres does a system(ld...) to do dynamic loading.
 * This will open several extra files in addition to those used by
 * Postgres.  We need to guarantee that there are file descriptors free
 * for ld to use.
 *
 * The current solution is to limit the number of file descriptors
 * that this code will allocate at one time: it leaves RESERVE_FOR_LD free.
 *
 * (Even though most dynamic loaders now use dlopen(3) or the
 * equivalent, the OS must still open several files to perform the
 * dynamic loading.  Keep this here.)
 */
#ifndef RESERVE_FOR_LD
#define RESERVE_FOR_LD	64
#endif

/*
 * We need to ensure that we have at least some file descriptors
 * available to postgreSQL after we've reserved the ones for LD,
 * so we set that value here.
 *
 * I think 10 is an appropriate value so that's what it'll be
 * for now.
 */
#ifndef FD_MINFREE
#define FD_MINFREE 50
#endif

#ifndef NOFILE
#define NOFILE 512
#endif


#define VFD_CLOSED (-1)

#define FileIsValid(file) \
	((file) > 0 && (file) < (int)VfdCache.size && GetVirtualFD(file)->fileName != NULL)

typedef struct vfd {
    int id;
    signed int fd; /* current FD, or VFD_CLOSED if none */
    unsigned short fdstate; /* bitflags for VFD's state */

    /* these are the assigned bits in fdstate: */
#define FD_DIRTY	(1 << 0)/* written to, but not yet fsync'd */
#define FD_TEMPORARY	(1 << 1)/* should be unlinked when closed */

    File nextFree; /* link to next free VFD, if in freelist */
    File newerFile;
    bool sweep_valid;
    bool pooled;
    bool private;
    long usage_count;
    time_t access_time;
    time_t newer_access_time;
    long seekPos; /* current logical file position */
    char fileName[512]; /* name of file, or NULL for unused VFD */
    int fileFlags; /* open(2) flags for opening the file */
    int fileMode; /* mode to pass to open(2) */
    int refCount; /*  counting references  */
    int key;
    pthread_mutex_t pin;
    pthread_t owner;
} Vfd;

typedef struct {
    char filename[512];
    Vfd* vfd;
} VfdEntry;

/*
 * Virtual File Descriptor array pointer and size.	This grows as
 * needed.	'File' values are indexes into this array.
 * Note that VfdCache[0] is not a usable VFD, just a list header.
 */
#ifndef MAX_FILE_SHARE
#define MAX_FILE_SHARE 1
#endif
#ifndef GROWVFDMULTIPLE
#define GROWVFDMULTIPLE 		32
#endif
#ifndef MAXVFDBLOCKS
#define MAXVFDBLOCKS		32 * 1024
#endif
#define MAXVIRTUALFILES  	GROWVFDMULTIPLE * MAXVFDBLOCKS

static int vfdmultiple = GROWVFDMULTIPLE;
static int vfdsharemax = MAX_FILE_SHARE;
static int vfdblockcount = MAXVFDBLOCKS;
static int vfdmax = MAXVIRTUALFILES;
static bool vfdoptimize = false;
static bool vfdautotune = false;

static struct {
    Vfd ** pointers;
    Size size;
    pthread_mutex_t guard;
    MemoryContext cxt;
} VfdCache;

static struct {
    HTAB* hash;
    pthread_mutex_t guard;
    MemoryContext cxt;
} VfdPool;

static pthread_mutexattr_t pinattr;

/*
 * Number of file descriptors known to be in use by VFD entries.
 */

/*
 * List of stdio FILEs opened with AllocateFile.
 *
 * Since we don't want to encourage heavy use of AllocateFile, it seems
 * OK to put a pretty small maximum limit on the number of simultaneously
 * allocated files.
 */

#define MAX_ALLOCATED_FILES  MAXBACKENDS * 2

static struct {
    int nfile;
    int numAllocatedFiles;
    int maxfiles;
    int checks;
    FILE * allocatedFiles[MAX_ALLOCATED_FILES];
    pthread_mutex_t guard;
} RealFiles;




/*
 * Number of temporary files opened during the current transaction;
 * this is used in generation of tempfile names.
 */
static long tempFileCounter = 0;

static HTAB* CreateFDHash(MemoryContext cxt);

static Vfd* HashScanFD(FileName filename, int fileflags, int filemode, bool * allocated);
static bool HashDropFD(Vfd* target);

static bool ActivateFile(Vfd* file);
static void RetireFile(Vfd* file);
static bool CheckFileAccess(Vfd* target);

static bool ReleaseFileIfNeeded();

static Vfd* AllocateVfd(FileName name, int fileflags, int filemode, bool private);
static void FreeVfd(Vfd* file);

static Vfd* GetVirtualFD(int index);
static Index InitializeBlock(int start);
static int GetVfdPoolSize();

static char *filepath(char* buf, char *filename, int len);
static long pg_nofile(void);
static bool CheckRealFileCount();

static void closeAllVfds();

/*
 * pg_fsync --- same as fsync except does nothing if -F switch was given
 */
int
pg_fsync(int fd) {
#ifdef MACOSX
    return fsync(fd);
#else
    return fdatasync(fd);
#endif
}

/*
 * pg_nofile: determine number of filedescriptors that fd.c is allowed to use
 */
static long
pg_nofile(void) {
    long no_files = 0;

    if (no_files == 0) {
        double fraction = GetFloatProperty("vfdallocation");
        /* need do this calculation only once */
#ifndef HAVE_SYSCONF
        no_files = (long) NOFILE;
#else
        no_files = sysconf(_SC_OPEN_MAX);
        if (no_files == -1) {
            elog(DEBUG, "pg_nofile: Unable to get _SC_OPEN_MAX using sysconf(); using %d", NOFILE);
            no_files = (long) NOFILE;
        } else {
            elog(DEBUG, "maximum number of open files %i", no_files);
        }

#endif

        if ((no_files - RESERVE_FOR_LD) < FD_MINFREE)
            elog(FATAL, "pg_nofile: insufficient File Descriptors in postmaster to start backend (%ld).\n"
                "                   O/S allows %ld, Postmaster reserves %d, We need %d (MIN) after that.",
                no_files - RESERVE_FOR_LD, no_files, RESERVE_FOR_LD, FD_MINFREE);
        if (fraction > 0) {
            no_files *= fraction;
        } else {
            no_files /= 2;
        }
    }

    return no_files;
}

static HTAB*
CreateFDHash(MemoryContext cxt) {
    HASHCTL ctl;

    MemSet(&ctl, 0, (int) sizeof (ctl));
    ctl.hash = string_hash;
    ctl.hcxt = cxt;
    ctl.keysize = 512;
    ctl.entrysize = sizeof (VfdEntry);

    return hash_create("fd hash", VfdCache.size, &ctl, HASH_ELEM | HASH_FUNCTION | HASH_CONTEXT);
}

static void
RetireFile(Vfd* vfdP) {
    int returnValue;

    if (vfdP->fd == VFD_CLOSED) {
        elog(DEBUG, "RetireFile closing closed file");
        return;
    }

    /* save the seek position */
    vfdP->seekPos = (long) lseek(vfdP->fd, 0L, SEEK_CUR);
    vfdP->usage_count = 0;
    vfdP->sweep_valid = false;
    Assert(vfdP->seekPos != -1);

    /* if we have written to the file, sync it before closing */
    if (vfdP->fdstate & FD_DIRTY) {
        returnValue = pg_fsync(vfdP->fd);
        Assert(returnValue != -1);
        vfdP->fdstate &= ~FD_DIRTY;
    }

    pthread_mutex_lock(&RealFiles.guard);
    /* close the file */
    returnValue = close(vfdP->fd);

    if (!returnValue) {
        RealFiles.nfile -= 1;
        DTRACE_PROBE3(mtpg, file__retired, vfdP->id, vfdP->fileName, RealFiles.nfile);
        vfdP->fd = VFD_CLOSED;
        vfdP->fileFlags &= ~(O_TRUNC | O_EXCL | O_CREAT);
    } else {
        perror("RetireFile");
    }
    pthread_mutex_unlock(&RealFiles.guard);
}

static Vfd*
HashScanFD(FileName filename, int fileflags, int filemode, bool * allocated) {
    bool found;
    VfdEntry* entry;
    Vfd* target = NULL;

    pthread_mutex_lock(&VfdPool.guard);

    entry = hash_search(VfdPool.hash, filename, HASH_ENTER, &found);

    if (found) {
        if (entry->vfd->refCount >= vfdsharemax ||
            ( entry->vfd->fileMode != filemode || entry->vfd->fileFlags != fileflags ) ) {
            entry->vfd->pooled = false;
            entry->vfd = NULL;
        } else {
            target = entry->vfd;
            Assert(strcmp(target->fileName, filename) == 0);
            target->refCount++;
        }
    }

    if (target == NULL) {
        target = AllocateVfd(filename, fileflags, filemode, false);
        target->pooled = true;
        entry->vfd = target;
        *allocated = true;
    }

    DTRACE_PROBE4(mtpg, file__search, target->id, filename, found, target->refCount);

    pthread_mutex_unlock(&VfdPool.guard);

    return target;
}

static bool
HashDropFD(Vfd * target) {
    bool found = false;
    VfdEntry* entry;

    pthread_mutex_lock(&VfdPool.guard);

    target->refCount--;

    if (target->refCount == 0) {
        if (target->pooled) {
            entry = hash_search(VfdPool.hash, (char*) target->fileName, HASH_REMOVE, &found);
            if (!found) {
                printf("not freed %s\n", target->fileName);
            } else {
                Assert(target == entry->vfd);
                entry->vfd->pooled = false;
                entry->vfd = NULL;
            }
        } else {
            found = true;
        }
    }

    DTRACE_PROBE4(mtpg, file__drop, target->id, target->fileName, found, target->refCount);

    pthread_mutex_unlock(&VfdPool.guard);

    return found;
}

static bool
ActivateFile(Vfd* vfdP) {

    Assert(vfdP->fd == VFD_CLOSED);
    errno = 0;

    while (vfdP->fd == VFD_CLOSED && errno == 0) {
        ReleaseFileIfNeeded();
        /*
         * The open could still fail for lack of file descriptors, eg due
         * to overall system file table being full.  So, be prepared to
         * release another FD if necessary...
         */
        pthread_mutex_lock(&RealFiles.guard);
        vfdP->fd = open(vfdP->fileName, vfdP->fileFlags, vfdP->fileMode);
        if (vfdP->fd < 0) {
            vfdP->fd = VFD_CLOSED;
            if (errno == EMFILE || errno == ENFILE) {
                /* try again */
                errno = 0;
            } else {
                /*  exit loop  */
            }
        } else {
            /*  freshly opened file, optimize */
            if (vfdoptimize) {
                FileOptimize(vfdP->id);
            } else {
                FileNormalize(vfdP->id);
            }
            RealFiles.nfile += 1;
            DTRACE_PROBE3(mtpg, file__activated, vfdP->id, vfdP->fileName, RealFiles.nfile);
        }
        pthread_mutex_unlock(&RealFiles.guard);
    }

    /* seek to the right position */
    if (vfdP->seekPos != 0L) {
        off_t check = lseek(vfdP->fd, vfdP->seekPos, SEEK_SET);
        if (check != vfdP->seekPos) {
            elog(NOTICE, "bad file activation during seek filename:%s, current: %d, seeked: %d", vfdP->fileName, vfdP->seekPos, check);
        }
    }

    return (vfdP->fd != VFD_CLOSED);
}

/*
 * Force one kernel file descriptor to be released (temporarily).
 */
bool
ReleaseDataFile() {
    ReleaseFileIfNeeded();
    return (true);
}

void
ShutdownVirtualFileSystem() {
    closeAllVfds();
}

void
InitVirtualFileSystem() {
    int counter;

    int share = GetIntProperty("vfdsharemax");
    bool opti = GetBoolProperty("vfdoptimize");
    bool autotune = GetBoolProperty("vfdautotune");

    if (share != 0) vfdsharemax = share;
    vfdoptimize = opti;
    vfdautotune = autotune;

    vfdmax = vfdmultiple * vfdblockcount;

    /* initialize header entry first time through */
    pthread_mutexattr_init(&pinattr);
#ifndef MACOSX
    pthread_mutexattr_setpshared(&pinattr, PTHREAD_PROCESS_PRIVATE);
    pthread_mutexattr_settype(&pinattr, PTHREAD_MUTEX_ERRORCHECK);
#endif		
    /*  set the max number of user fd's  */
    RealFiles.maxfiles = pg_nofile();
    RealFiles.checks = RealFiles.maxfiles;

    VfdCache.cxt = GetEnvMemoryContext();

    VfdCache.pointers = MemoryContextAlloc(VfdCache.cxt, (sizeof (Vfd*) * vfdblockcount));
    VfdCache.pointers[0] = (Vfd *) MemoryContextAlloc(VfdCache.cxt, sizeof (Vfd) * vfdmultiple);
    if (VfdCache.pointers == NULL || VfdCache.pointers[0] == NULL) {
        elog(FATAL, "Memory exhusted in File Manager");
    }

    InitializeBlock(0);

    /*  file pool cache */
    VfdCache.size = vfdmultiple;
    pthread_mutex_init(&VfdCache.guard, &pinattr);

    /*  file pool hash */
    VfdPool.hash = CreateFDHash(GetEnvMemoryContext());
    pthread_mutex_init(&VfdPool.guard, &pinattr);

    /*  real file tracking */
    RealFiles.nfile = 0;
    RealFiles.numAllocatedFiles = 0;
    for (counter = 0; counter < MAX_ALLOCATED_FILES; counter++) {
        RealFiles.allocatedFiles[counter] = NULL;
    }
    pthread_mutex_init(&RealFiles.guard, &pinattr);

    /* set the start and the end of the free 
            blocks to the right places 
            MKS  12.12.2001 */
    GetVirtualFD(vfdmultiple - 1)->nextFree = 0;
    GetVirtualFD(0)->nextFree = 1;

}

static Vfd*
GetVirtualFD(int index) {
    int sect = index / vfdmultiple;
    int pos = index % vfdmultiple;
    return VfdCache.pointers[sect] + pos;
}

static Index
InitializeBlock(int start) {
    int counter;

    for (counter = start; counter < vfdmultiple + start; counter++) {
        Vfd* target = GetVirtualFD(counter);
        MemSet((char*) target, 0, sizeof (Vfd));
        target->id = counter;
        target->nextFree = counter + 1;
        target->fd = VFD_CLOSED;
        target->usage_count = 0;
        target->sweep_valid = false;
        target->newerFile = -1;
        target->refCount = 0;
        target->pooled = false;
        pthread_mutex_init(&target->pin, &pinattr);
    }
    return start;
}

static Vfd*
AllocateVfd(FileName name, int fileflags, int filemode, bool private) {
    Index i;
    File file;
    Vfd* target;
    Vfd* list = GetVirtualFD(0);

    pthread_mutex_lock(&list->pin);
    if (list->nextFree == 0) {

        /*
         * The free list is empty so it is time to increase the size of
         * the array.  We choose to double it each time this happens.
         * However, there's not much point in starting *real* small.
         */

        Size newCacheSize;
        Vfd* block;
        int position, section;

        pthread_mutex_lock(&VfdCache.guard);
        newCacheSize = VfdCache.size;
        block = (Vfd*) MemoryContextAlloc(VfdCache.cxt, newCacheSize * sizeof (Vfd));

        if (block == NULL) {
            elog(FATAL, "Memory exhausted");
        }
        if (vfdmultiple + VfdCache.size > vfdmax) {
            elog(FATAL, "The maximum number of virtual files have been used");
        }

        for (position = VfdCache.size;
                position < (VfdCache.size + newCacheSize);
                position += vfdmultiple) {
            section = position / vfdmultiple;
            VfdCache.pointers[section] = block + (position - VfdCache.size);
            InitializeBlock(position);
        }
        /* set the start and the end of the free 
                blocks to the right places 
                MKS  12.12.2001 */

        GetVirtualFD(position - 1)->nextFree = 0;
        list->nextFree = VfdCache.size;

        VfdCache.size = position;
        DTRACE_PROBE1(mtpg, file__poolsize, VfdCache.size);
        pthread_mutex_unlock(&VfdCache.guard);

    }
    file = list->nextFree;

    target = GetVirtualFD(file);

    list->nextFree = target->nextFree;

    strncpy(target->fileName, name, strlen(name));
    /*  make sure that if this is file, if shared, 
        does not have exclusive or create or trunc
     */
    if (!private) target->fileFlags = fileflags & ~(O_TRUNC | O_EXCL | O_CREAT);
    else target->fileFlags = fileflags;

    target->fileMode = filemode;
    target->seekPos = 0;
    target->fdstate = 0x0;
    /*  allocating so reference it  */
    Assert(target->refCount == 0);
    target->refCount = 1;
    target->fd = VFD_CLOSED;
    target->nextFree = -1;
    target->pooled = false;
    target->private = private;

    pthread_mutex_unlock(&list->pin);

    return target;
}

static void
FreeVfd(Vfd* vfdP) {
    Vfd *list = GetVirtualFD(0);
    pthread_mutex_lock(&list->pin);
    Assert(vfdP->refCount == 0);
    Assert(vfdP->fd == VFD_CLOSED);
    Assert(vfdP->pooled == false);
    memset(vfdP->fileName, 0x00, 512);
    vfdP->sweep_valid = false;
    vfdP->nextFree = list->nextFree;
    list->nextFree = vfdP->id;
    pthread_mutex_unlock(&list->pin);

}

/* filepath()
 * Convert given pathname to absolute.
 *
 * (Generally, this isn't actually necessary, considering that we
 * should be cd'd into the database directory.  Presently it is only
 * necessary to do it in "bootstrap" mode.	Maybe we should change
 * bootstrap mode to do the cd, and save a few cycles/bytes here.)
 */
static char *
filepath(char* buf, char *filename, int len) {
    /* Not an absolute path name? Then fill in with database path... */
    if (*filename != SEP_CHAR) {
        snprintf(buf, len, "%s%c%s", GetDatabasePath(), SEP_CHAR, filename);
        if (strlen(buf) == len) {
            elog(ERROR, "file path for file name: %s is too long", filename);
        }
    } else {
        strncpy(buf, filename, len);
    }

    return buf;
}

static bool
CheckFileAccess(Vfd* target) {
    int trys = 0;

    errno = 0;

    Assert(pthread_equal(target->owner, pthread_self()));

    while (target->fd == VFD_CLOSED && trys++ < 5) {
        if (!ActivateFile(target)) {
            char* err = strerror(errno);
            elog(NOTICE, "bad file activation: %s loc: %d err: %s", target->fileName, target->seekPos, err);
            errno = 0;
        }
    }

    if (target->fd == VFD_CLOSED) return FALSE;

    target->usage_count += 1;
    time(&target->access_time);
    target->sweep_valid = false;

    return TRUE;
}

static File
fileNameOpenFile(FileName fileName,
        int fileFlags,
        int fileMode) {
    Vfd *vfdP = NULL;
    errno = 0;
    bool private = (IsDBWriter() || IsPoolsweep() || IsBootstrapProcessingMode()
            || (fileFlags & (O_CREAT | O_EXCL | O_TRUNC))) ? true : false;


    if (fileName == NULL) {
        elog(DEBUG, "fileNameOpenFile: NULL fname");
        return VFD_CLOSED;
    }

    if (strlen(fileName) > 511 && !private) {
        elog(DEBUG, "fileNameOpenFile: file path too long, going private");
        private = true;
    }

        if (!private && vfdsharemax > 1) {
            bool allocated = false;
            vfdP = HashScanFD(fileName, fileFlags, fileMode, &allocated);
            Assert(vfdP != NULL);
            pthread_mutex_lock(&vfdP->pin);
            vfdP->owner = pthread_self();
            allocated = CheckFileAccess(vfdP);
            vfdP->owner = 0;
            pthread_mutex_unlock(&vfdP->pin);
            if ( !allocated ) {
                HashDropFD(vfdP);
                vfdP = NULL;
            }
        } else {
            vfdP = AllocateVfd(fileName, fileFlags, fileMode, private);
            /* activate your file to make sure that it can be created, this is important in bootstrap mode 
             * because if the allocation fails, we try again with file creation 
             */
            Assert(vfdP != NULL);
            if ( !ActivateFile(vfdP) ) {
                vfdP->refCount = 0;
                FreeVfd(vfdP);
                vfdP = NULL;
            }
        }
            
        if (vfdP == NULL) {
            return VFD_CLOSED;
        }
        
        DTRACE_PROBE2(mtpg, file__opened, vfdP->id, vfdP->fileName);

    return vfdP->id;
}

/*
 * open a file in the database directory ($PGDATA/base/...)
 */
File
FileNameOpenFile(FileName fileName, int fileFlags, int fileMode) {
    File fd;
    char* fname = palloc(512);

    if (strlen(fileName) > 512) {
        elog(ERROR, "cannot open file -- %s, path too long", fileName);
    }
    memset(fname, 0x00, 512);
    filepath(fname, fileName, 512);

    fd = fileNameOpenFile(fname, fileFlags, fileMode);

    pfree(fname);

    return fd;
}

/*
 * open a file in an arbitrary directory
 */
File
PathNameOpenFile(FileName fileName, int fileFlags, int fileMode) {
    File file = -1;
    char* fname = palloc(512);

    if (strlen(fileName) > 512) {
        elog(ERROR, "cannot open file -- %s, path too long", fileName);
    }
    memset(fname, 0x00, 512);
    strncpy(fname, fileName, strlen(fileName));

    file = fileNameOpenFile(fname, fileFlags, fileMode);

    pfree(fname);

    return file;
}

/*
 * Open a temporary file that will disappear when we close it.
 *
 * This routine takes care of generating an appropriate tempfile name.
 * There's no need to pass in fileFlags or fileMode either, since only
 * one setting makes any sense for a temp file.
 */
File
OpenTemporaryFile(void) {
    char tempfilename[64];
    File file;
    Env* env = GetEnv();
    int count = 0;
    /*
     * Generate a tempfile name that's unique within the current
     * transaction
     */
    snprintf(tempfilename, sizeof (tempfilename),
            "pg_sorttemp%d.%d.%ld", MyProcPid, (int) pthread_self(), tempFileCounter++);

    /* Open the file */

#ifndef __CYGWIN32__
    file = FileNameOpenFile(tempfilename,
            O_RDWR | O_CREAT | O_TRUNC, 0600);
#else
    file = FileNameOpenFile(tempfilename,
            O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0600);
#endif

    if (file <= 0) {
        elog(DEBUG, "Failed to create temporary file %s", tempfilename);
    }
    pthread_mutex_lock(&GetVirtualFD(file)->pin);
    /* Mark it for deletion at close or EOXact */
    GetVirtualFD(file)->fdstate |= FD_TEMPORARY;
    pthread_mutex_unlock(&GetVirtualFD(file)->pin);

    while (env->temps[count] != 0 && count < MAX_PRIVATE_FILES) count++;

    if (count == MAX_PRIVATE_FILES) {
        FileClose(file);
        elog(ERROR, "Too many temporary files requested");
    }

    env->temps[count] = file;
    return file;
}

/*
 * close a file when done with it
 */
void
FileClose(File file) {
    Vfd* target = GetVirtualFD(file);
    bool free = false;

    if (vfdsharemax <= 1 || target->private) {
        Assert(target->refCount == 1);
        free = true;
        target->refCount = 0;
    } else {
        free = HashDropFD(target);
    }

    pthread_mutex_lock(&target->pin);

    if (target->fdstate & FD_TEMPORARY) {
        int track = 0;
        Env* env = GetEnv();

        for (track = 0; track < MAX_PRIVATE_FILES; track++) {
            if (env->temps[track] == file) env->temps[track] = 0;
        }
    }

    DTRACE_PROBE2(mtpg, file__closed, file, target->fileName);

    if (free) {
        if (target->fd != VFD_CLOSED) {
            RetireFile(target);
        }
        /*
         * Delete the file if it was temporary
         */
        if (target->fdstate & FD_TEMPORARY)
            unlink(target->fileName);
        /*  put back to pool  */
        FreeVfd(target);
    }

    pthread_mutex_unlock(&target->pin);
}

char*
FileGetName(File file) {
    Vfd* target = GetVirtualFD(file);

    Assert(FileIsValid(file));
    return target->fileName;
}

/*
 * close a file and forcibly delete the underlying Unix file
 */
void
FileUnlink(File file) {
    Assert(FileIsValid(file));

    Vfd* virtf = GetVirtualFD(file);
    pthread_mutex_lock(&virtf->pin);
    if (virtf->fd != VFD_CLOSED) {
        RetireFile(virtf);
    }
    unlink(virtf->fileName);
    pthread_mutex_unlock(&virtf->pin);

    FileClose(file);
}

/*
 * close a file and forcibly delete the underlying Unix file
 */
void
FileRename(File file, char* newname) {
    Assert(FileIsValid(file));

    Vfd* virtf = GetVirtualFD(file);
    pthread_mutex_lock(&virtf->pin);
    if (virtf->fd != VFD_CLOSED) {
        RetireFile(virtf);
    }
    rename(virtf->fileName, newname);
    pthread_mutex_unlock(&virtf->pin);

    FileClose(file);
}

int
FileRead(File file, char *buffer, int amount) {
    ssize_t blit;
    int request = amount;
    Vfd* target = GetVirtualFD(file);
    Assert(FileIsValid(file));

    errno = 0;
        
    if (!CheckFileAccess(target)) return -1;

    while (amount > 0) {
        blit = read(target->fd, buffer, amount);
        if (blit < 0) {
            char* err = strerror(errno);
            errno = 0;
            elog(NOTICE, "bad read file: %s loc: %d err: %s", target->fileName, target->seekPos, err);
            return -1;
        } else if (blit == 0) {
            /* EOF  */
            blit = lseek(target->fd,0,SEEK_END);
            if ( blit == target->seekPos ) {
                target->seekPos += blit;
                return (request - amount);
            } else {
                lseek(target->fd,target->seekPos + request - amount,SEEK_SET);
                blit = 0;
            }
        }
        amount -= blit;
        buffer += blit;
    }

    target->seekPos += request;

    return request;
}

int
FileWrite(File file, char *buffer, int amount) {
    Vfd* target = GetVirtualFD(file);
    int request = amount;
    
    errno = 0;

    if (!CheckFileAccess(target)) return -1;

    while (amount > 0) {
        
        ssize_t blit = write(target->fd, buffer, amount);
        if (blit < 0) {
            char* err = strerror(errno);
            elog(NOTICE, "bad write file: %s loc: %d err: %s", target->fileName, target->seekPos, err);
            return -1;
        } else if (blit == 0) {
            elog(NOTICE, "partial write %s", target->fileName);
            return (request - amount);
        }
        buffer += blit;
        amount -= blit;
    }

    /* mark the file as needing fsync */
    target->fdstate |= FD_DIRTY;

    return request;
}

long
FileSeek(File file, long offset, int whence) {
    Vfd* target = GetVirtualFD(file);
    off_t blit = 0;
    int fails = 0;

    if (target->fd == VFD_CLOSED) {
        switch (whence) {
            case SEEK_SET:
                target->seekPos = offset;
                break;
            case SEEK_CUR:
                target->seekPos += offset;
                break;
            case SEEK_END:
                if (!CheckFileAccess(target)) return -1;
                blit = lseek(target->fd, offset, whence);
                if (blit < 0) {
                    perror("FileSeek");
                    return -1;
                } else {
                    target->seekPos = blit;
                }
                break;
            default:
                elog(DEBUG, "FileSeek: invalid whence: %d", whence);
                break;
        }
    } else {
        if (!CheckFileAccess(target)) return -1;
        blit = lseek(target->fd, offset, whence);
        if (blit < 0) {
            char* err = strerror(errno);
            elog(NOTICE, "bad seek file: %s loc: %d err: %s", target->fileName, target->seekPos, err);
            if (fails++ > 5) {
                return -1;
            }
        } else {
            target->seekPos = blit;
        }
    }
    return target->seekPos;
}

int
FileTruncate(File file, long offset) {
    int returnCode;
    Vfd* target = GetVirtualFD(file);

    Assert(FileIsValid(file));

    FileSync(file);

    returnCode = ftruncate(target->fd, offset);
    pg_fsync(target->fd);

    return returnCode;
}

/*
 * FileSync --- if a file is marked as dirty, fsync it.
 *
 * The FD_DIRTY bit is slightly misnamed: it doesn't mean that we need to
 * write the file, but that we *have* written it and need to execute an
 * fsync() to ensure the changes are down on disk before we mark the current
 * transaction committed.
 *
 * FD_DIRTY is set by FileWrite or by an explicit FileMarkDirty() call.
 * It is cleared after successfully fsync'ing the file.  FileClose() will
 * fsync a dirty File that is about to be closed, since there will be no
 * other place to remember the need to fsync after the VFD is gone.
 *
 * Note that the DIRTY bit is logically associated with the actual disk file,
 * not with any particular kernel FD we might have open for it.  We assume
 * that fsync will force out any dirty buffers for that file, whether or not
 * they were written through the FD being used for the fsync call --- they
 * might even have been written by some other backend!
 *
 * Note also that LruDelete currently fsyncs a dirty file that it is about
 * to close the kernel file descriptor for.  The idea there is to avoid
 * having to re-open the kernel descriptor later.  But it's not real clear
 * that this is a performance win; we could end up fsyncing the same file
 * multiple times in a transaction, which would probably cost more time
 * than is saved by avoiding an open() call.  This should be studied.
 *
 * This routine used to think it could skip the fsync if the file is
 * physically closed, but that is now WRONG; see comments for FileMarkDirty.
 */
int
FileSync(File file) {
    int returnCode;
    Vfd* target = GetVirtualFD(file);

    if (!(target->fdstate & FD_DIRTY)) {
        /* Need not sync if file is not dirty. */
        returnCode = 0;
    } else if (disableFsync) {
        /* Don't force the file open if pg_fsync isn't gonna sync it. */
        returnCode = 0;
        target->fdstate &= ~FD_DIRTY;
    } else {

        /*
         * We don't use FileAccess() because we don't want to force the
         * file to the front of the LRU ring; we aren't expecting to
         * access it again soon.
         */
        if (!CheckFileAccess(target)) return -1;
        returnCode = pg_fsync(target->fd);
        if (returnCode == 0)
            target->fdstate &= ~FD_DIRTY;
    }
    return returnCode;
}

int
FilePin(File file, int key) {
    Vfd* target = GetVirtualFD(file);

    while (pthread_mutex_lock(&target->pin)) {
        perror("FilePin");
    }
    Assert(target->owner == 0);
 
    target->owner = pthread_self();
    target->key = key;

    Assert(target->owner != 0);
}

int
FileUnpin(File file, int key) {
    Vfd* target = GetVirtualFD(file);
    int err = 0;

    target->owner = 0;
    target->key = 0;
    err = pthread_mutex_unlock(&target->pin);

    switch (err) {
        case 0:
            break;
        case EPERM:
            elog(DEBUG, "no lock owner");
            break;
        default:
            printf("error %i", err);
    }

    return 0;
}

/*
 * FileMarkDirty --- mark a file as needing fsync at transaction commit.
 *
 * Since FileWrite marks the file dirty, this routine is not needed in
 * normal use.	It is called when the buffer manager detects that some other
 * backend has written out a shared buffer that this backend dirtied (but
 * didn't write) in the current xact.  In that scenario, we need to fsync
 * the file before we can commit.  We cannot assume that the other backend
 * has fsync'd the file yet; we need to do our own fsync to ensure that
 * (a) the disk page is written and (b) this backend's commit is delayed
 * until the write is complete.
 *
 * Note we are assuming that an fsync issued by this backend will write
 * kernel disk buffers that were dirtied by another backend.  Furthermore,
 * it doesn't matter whether we currently have the file physically open;
 * we must fsync even if we have to re-open the file to do it.
 */
void
FileMarkDirty(File file) {
    Vfd* target = GetVirtualFD(file);
    pthread_mutex_lock(&target->pin);
    target->fdstate |= FD_DIRTY;
    pthread_mutex_unlock(&target->pin);
}

int
FileOptimize(File file) {
    Vfd *target = GetVirtualFD(file);
    int flag = 0;

    if (target->fd == VFD_CLOSED) return 0;

#ifdef SUNOS
    flag = DIRECTIO_ON;

    directio(target->fd, flag);
#endif
    return 1;
}

int
FileNormalize(File file) {
    Vfd *target = GetVirtualFD(file);
    int flag = 0;

    if (target->fd == VFD_CLOSED) return 0;

#ifdef SUNOS
    flag = DIRECTIO_OFF;

    directio(target->fd, flag);
#endif
    return 1;
}

/*
 * Routines that want to use stdio (ie, FILE*) should use AllocateFile
 * rather than plain fopen().  This lets fd.c deal with freeing FDs if
 * necessary to open the file.	When done, call FreeFile rather than fclose.
 *
 * Note that files that will be open for any significant length of time
 * should NOT be handled this way, since they cannot share kernel file
 * descriptors with other files; there is grave risk of running out of FDs
 * if anyone locks down too many FDs.  Most callers of this routine are
 * simply reading a config file that they will read and close immediately.
 *
 * fd.c will automatically close all files opened with AllocateFile at
 * transaction commit or abort; this prevents FD leakage if a routine
 * that calls AllocateFile is terminated prematurely by elog(DEBUG).
 */

FILE *
AllocateFile(char *name, char *mode) {
    FILE* file = NULL;
    int ind = 0;
    Env* env = GetEnv();
    errno = 0;
    while (env->falloc[ind] != NULL && ind < MAX_PRIVATE_FILES) ind++;
    if (ind == MAX_PRIVATE_FILES) {
        elog(ERROR, "AllocateFile: too many private FDs demanded");
    }

    while (file == NULL && errno == 0) {
        ReleaseFileIfNeeded();
        file = fopen(name, mode);
        if (errno == EMFILE || errno == ENFILE) {
            errno = 0;
        }
    }

    if (file != NULL) {
        pthread_mutex_lock(&RealFiles.guard);
        if (RealFiles.numAllocatedFiles >= MAX_ALLOCATED_FILES) {
            pthread_mutex_unlock(&RealFiles.guard);
            fclose(file);
            elog(ERROR, "AllocateFile: too many private FDs demanded");
        }
        RealFiles.allocatedFiles[RealFiles.numAllocatedFiles++] = file;
        env->falloc[ind] = file;
        pthread_mutex_unlock(&RealFiles.guard);
    }

    return file;
}

void
FreeFile(FILE *file) {
    int i;
    Env* env = GetEnv();

    /* Remove file from list of allocated files, if it's present */
    pthread_mutex_lock(&RealFiles.guard);
    for (i = RealFiles.numAllocatedFiles; --i >= 0;) {
        if (RealFiles.allocatedFiles[i] == file) {
            RealFiles.allocatedFiles[i] = RealFiles.allocatedFiles[--RealFiles.numAllocatedFiles];
            break;
        }
    }


    if (i < 0)
        elog(NOTICE, "FreeFile: file was not obtained from AllocateFile");

    for (i = 0; i < MAX_PRIVATE_FILES; i++) {
        if (env->falloc[i] == file) env->falloc[i] = NULL;
    }

    fclose(file);
    pthread_mutex_unlock(&RealFiles.guard);
}

/*
 *  synchronize all the OS files to 
 *  a base change made in the DB
 */
int
FileBaseSync(File file, long pos) {
    int count = 0;
    Index i;

    pthread_mutex_lock(&VfdCache.guard);

    Vfd* base = GetVirtualFD(file);
    for (i = 1; i < VfdCache.size; i++) {
        Vfd* target = GetVirtualFD(i);

        if (target->id == base->id) continue;

        pthread_mutex_lock(&target->pin);

        if (target->refCount > 0) {
            if (strcmp(base->fileName, target->fileName) == 0) {
                if (target->fd != VFD_CLOSED) {
                    RetireFile(target);
                }
                if (target->seekPos > pos) target->seekPos = pos;
                count++;
            }
        }

        pthread_mutex_unlock(&target->pin);
    }

    pthread_mutex_unlock(&VfdCache.guard);

    return count;
}

/*
 * closeAllVfds
 *
 * Force all VFDs into the physically-closed state, so that the fewest
 * possible number of kernel file descriptors are in use.  There is no
 * change in the logical state of the VFDs.
 */
void
closeAllVfds() {
    Index i;

    pthread_mutex_lock(&VfdCache.guard);

    for (i = 1; i < VfdCache.size; i++) {
        Vfd* target = GetVirtualFD(i);
        pthread_mutex_lock(&target->pin);
        if (target->refCount > 0) {
            target->refCount = 1;
            pthread_mutex_unlock(&target->pin);
            FileClose(i);
        } else {
            pthread_mutex_unlock(&target->pin);
        }
    }
    pthread_mutex_unlock(&VfdCache.guard);
}

bool
ReleaseFileIfNeeded() {
    int sweep = 0;
    bool success = false;
    Vfd* target;
    File close = -1;
    time_t access;


    while (CheckRealFileCount()) {
        /*  first try and use hints fro a previous scan */
        close = GetVirtualFD(0)->newerFile;
        if (close > 0) {
            target = GetVirtualFD(close);
            GetVirtualFD(0)->newerFile = target->newerFile;
        } else {
            int poolsize = GetVfdPoolSize();

            close = -1;
            time(&access);
            for (sweep = 0; sweep < poolsize; sweep++) {
                target = GetVirtualFD(sweep);

                if (pthread_mutex_trylock(&target->pin)) {
                    /* only trylock  */
                    continue;
                }
                if (target->fd == VFD_CLOSED) {
                    pthread_mutex_unlock(&target->pin);
                    continue;
                }
                Assert(target->owner == 0);
                if (difftime(access, target->access_time) > 0) {
                    target->newer_access_time = access;
                    access = target->access_time;
                    target->newerFile = close;
                    close = sweep;

                    target->sweep_valid = true;
                }
                pthread_mutex_unlock(&target->pin);
            }

            if (close > 0) {
                target = GetVirtualFD(close);
            }
            pthread_mutex_lock(&GetVirtualFD(0)->pin);
            GetVirtualFD(0)->newerFile = target->newerFile;
            pthread_mutex_unlock(&GetVirtualFD(0)->pin);
        }


        if (target != NULL) {
            pthread_mutex_lock(&target->pin);
            if (target->sweep_valid && target->fd != VFD_CLOSED) {
                RetireFile(target);
            }
            pthread_mutex_unlock(&target->pin);
        }
    }
    return success;
}

static int
GetVfdPoolSize() {
    int size = 0;
    pthread_mutex_lock(&VfdCache.guard);
    size = VfdCache.size;
    pthread_mutex_unlock(&VfdCache.guard);
    return size;
}

static bool
CheckRealFileCount() {
    int size = 0;

    pthread_mutex_lock(&RealFiles.guard);
    size = RealFiles.nfile + RealFiles.numAllocatedFiles;
    DTRACE_PROBE3(mtpg, file__maxcheck, vfdsharemax, size, RealFiles.maxfiles);
    if (vfdautotune == true) {
        if ((size >= RealFiles.maxfiles * 0.9) && vfdsharemax < 64) {
            if (RealFiles.checks++ >= RealFiles.maxfiles) {
                RealFiles.checks = 0;
                vfdsharemax += 1;
            }
        } else if (size <= RealFiles.maxfiles * 0.20 && vfdsharemax > 1) {
            if (RealFiles.checks++ >= RealFiles.maxfiles) {
                RealFiles.checks == 0;
                vfdsharemax -= 1;
            }
        }
    }

    pthread_mutex_unlock(&RealFiles.guard);
    return (size >= RealFiles.maxfiles);
}

void
AtEOXact_Files(void) {
    int count = 0;
    Env* env = GetEnv();
    for (count = 0; count < MAX_PRIVATE_FILES; count++) {
        if (env->temps[count] != 0) FileClose(env->temps[count]);
        env->temps[count] = 0;
        if (env->falloc[count] != NULL) FreeFile(env->falloc[count]);
        env->falloc[count] = NULL;
    }
    return;
}

