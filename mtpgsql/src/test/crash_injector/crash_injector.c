/*-------------------------------------------------------------------------
 *
 * crash_injector.c
 *	  Test-only LD_PRELOAD / DYLD_INSERT_LIBRARIES helper.
 *
 * Interposes write(2)/pwrite(2) and fsync(2) (plus F_FULLFSYNC and, off
 * Darwin, fdatasync) and raises SIGKILL after N counted calls. This is
 * NOT linked into libweaver, libweaver_jni, or postgres. Crash injection
 * stays outside production code.
 *
 * Env:
 *	WEAVER_CRASH_AFTER_WRITES	kill after this many counted ops (>0)
 *	WEAVER_CRASH_MIN_BYTES		only count writes of at least this size
 *				(default 4096). fsync is not size-filtered.
 *	WEAVER_CRASH_GATE		if set, do not count until this path
 *				exists (so init is not the crash target)
 *	WEAVER_CRASH_ON			write (default) | fsync | both
 *	WEAVER_CRASH_PATH		if set, only count an fd whose path
 *				contains this substring (Darwin F_GETPATH,
 *				Linux /proc/self/fd)
 *
 *-------------------------------------------------------------------------
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <dlfcn.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#ifdef __APPLE__
#ifndef F_FULLFSYNC
#define F_FULLFSYNC 51
#endif
#define CRASH_INTERPOSE(_replacement, _replacee) \
	__attribute__((used)) static struct { \
		const void *replacement; \
		const void *replacee; \
	} _interpose_##_replacee \
	__attribute__((section("__DATA,__interpose"))) = { \
		(const void *)&_replacement, \
		(const void *)&_replacee \
	};

/* Bind the real kernel traps, not the interposed symbols. */
extern ssize_t darwin_write_nocancel(int, const void *, size_t)
	__asm__("_write$NOCANCEL");
extern ssize_t darwin_pwrite_nocancel(int, const void *, size_t, off_t)
	__asm__("_pwrite$NOCANCEL");
extern int darwin_fsync_nocancel(int)
	__asm__("_fsync$NOCANCEL");
extern int darwin_fcntl_nocancel(int, int, ...)
	__asm__("_fcntl$NOCANCEL");
#else
typedef ssize_t (*write_fn)(int, const void *, size_t);
typedef ssize_t (*pwrite_fn)(int, const void *, size_t, off_t);
typedef int (*fdatasync_fn)(int);
static write_fn real_write;
static pwrite_fn real_pwrite;
static fdatasync_fn real_fdatasync;
#endif

typedef int (*fsync_fn)(int);
typedef int (*fcntl_fn)(int, int, ...);

#ifndef __APPLE__
static fsync_fn real_fsync;
static fcntl_fn real_fcntl;
#endif

static volatile unsigned long write_count;
static unsigned long write_limit;
static size_t min_bytes = 4096;
static int armed;
static int crash_on_write = 1;
static int crash_on_fsync;
static const char *path_filter;

static int
gate_open(void)
{
	const char *gate = getenv("WEAVER_CRASH_GATE");

	if (gate == NULL || gate[0] == '\0')
		return 1;
	return access(gate, F_OK) == 0;
}

static int
path_selected(int fd)
{
	char buf[PATH_MAX];

	if (path_filter == NULL || path_filter[0] == '\0')
		return 1;

#ifdef __APPLE__
	if (darwin_fcntl_nocancel(fd, F_GETPATH, buf) != 0)
		return 0;
#else
	{
		char link[64];
		ssize_t n;

		snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
		n = readlink(link, buf, sizeof(buf) - 1);
		if (n < 0)
			return 0;
		buf[n] = '\0';
	}
#endif
	return strstr(buf, path_filter) != NULL;
}

static void
count_and_maybe_kill(void)
{
	unsigned long n;

	if (!armed || !gate_open())
		return;

	n = __sync_add_and_fetch(&write_count, 1);
	if (n >= write_limit)
		kill(getpid(), SIGKILL);
}

static void
maybe_crash_write(int fd, ssize_t nbyte)
{
	if (!crash_on_write)
		return;
	if (nbyte < 0 || (size_t) nbyte < min_bytes)
		return;
	if (!path_selected(fd))
		return;
	count_and_maybe_kill();
}

static void
maybe_crash_fsync(int fd, int rc)
{
	if (!crash_on_fsync || rc != 0)
		return;
	if (!path_selected(fd))
		return;
	count_and_maybe_kill();
}

static ssize_t
crash_write(int fd, const void *buf, size_t nbyte)
{
	ssize_t r;
#ifdef __APPLE__
	r = darwin_write_nocancel(fd, buf, nbyte);
#else
	if (real_write == NULL)
		real_write = (write_fn) dlsym(RTLD_NEXT, "write");
	if (real_write == NULL) {
		errno = ENOSYS;
		return -1;
	}
	r = real_write(fd, buf, nbyte);
#endif
	maybe_crash_write(fd, r);
	return r;
}

static ssize_t
crash_pwrite(int fd, const void *buf, size_t nbyte, off_t offset)
{
	ssize_t r;
#ifdef __APPLE__
	r = darwin_pwrite_nocancel(fd, buf, nbyte, offset);
#else
	if (real_pwrite == NULL)
		real_pwrite = (pwrite_fn) dlsym(RTLD_NEXT, "pwrite");
	if (real_pwrite == NULL) {
		errno = ENOSYS;
		return -1;
	}
	r = real_pwrite(fd, buf, nbyte, offset);
#endif
	maybe_crash_write(fd, r);
	return r;
}

static int
crash_fsync(int fd)
{
	int r;

#ifdef __APPLE__
	r = darwin_fsync_nocancel(fd);
#else
	if (real_fsync == NULL)
		real_fsync = (fsync_fn) dlsym(RTLD_NEXT, "fsync");
	if (real_fsync == NULL) {
		errno = ENOSYS;
		return -1;
	}
	r = real_fsync(fd);
#endif
	maybe_crash_fsync(fd, r);
	return r;
}

static int
crash_fcntl(int fd, int cmd, uintptr_t arg)
{
	int r;

#ifdef __APPLE__
	r = darwin_fcntl_nocancel(fd, cmd, arg);
#else
	if (real_fcntl == NULL)
		real_fcntl = (fcntl_fn) dlsym(RTLD_NEXT, "fcntl");
	if (real_fcntl == NULL) {
		errno = ENOSYS;
		return -1;
	}
	r = real_fcntl(fd, cmd, arg);
#endif
#ifdef F_FULLFSYNC
	if (cmd == F_FULLFSYNC)
		maybe_crash_fsync(fd, r);
#endif
	return r;
}

#ifndef __APPLE__
static int
crash_fdatasync(int fd)
{
	int r;

	if (real_fdatasync == NULL)
		real_fdatasync = (fdatasync_fn) dlsym(RTLD_NEXT, "fdatasync");
	if (real_fdatasync == NULL) {
		errno = ENOSYS;
		return -1;
	}
	r = real_fdatasync(fd);
	maybe_crash_fsync(fd, r);
	return r;
}
#endif

#ifdef __APPLE__
CRASH_INTERPOSE(crash_write, write)
CRASH_INTERPOSE(crash_pwrite, pwrite)
CRASH_INTERPOSE(crash_fsync, fsync)
CRASH_INTERPOSE(crash_fcntl, fcntl)
#else
ssize_t
write(int fd, const void *buf, size_t nbyte)
{
	return crash_write(fd, buf, nbyte);
}

ssize_t
pwrite(int fd, const void *buf, size_t nbyte, off_t offset)
{
	return crash_pwrite(fd, buf, nbyte, offset);
}

int
fsync(int fd)
{
	return crash_fsync(fd);
}

int
fdatasync(int fd)
{
	return crash_fdatasync(fd);
}

int
fcntl(int fd, int cmd, ...)
{
	uintptr_t arg = 0;
	va_list ap;

	va_start(ap, cmd);
	arg = va_arg(ap, uintptr_t);
	va_end(ap);
	return crash_fcntl(fd, cmd, arg);
}
#endif

__attribute__((constructor))
static void
crash_injector_ctor(void)
{
	const char *lim;
	const char *minb;
	const char *on;
	char *end = NULL;
	unsigned long v;

	lim = getenv("WEAVER_CRASH_AFTER_WRITES");
	if (lim == NULL || lim[0] == '\0')
		return;

	errno = 0;
	v = strtoul(lim, &end, 10);
	if (errno != 0 || end == lim || v == 0)
		return;

	minb = getenv("WEAVER_CRASH_MIN_BYTES");
	if (minb != NULL && minb[0] != '\0') {
		unsigned long mb = strtoul(minb, NULL, 10);
		if (mb > 0)
			min_bytes = (size_t) mb;
	}

	on = getenv("WEAVER_CRASH_ON");
	if (on != NULL && on[0] != '\0') {
		if (strcmp(on, "fsync") == 0) {
			crash_on_write = 0;
			crash_on_fsync = 1;
		} else if (strcmp(on, "both") == 0) {
			crash_on_write = 1;
			crash_on_fsync = 1;
		} else if (strcmp(on, "write") == 0) {
			crash_on_write = 1;
			crash_on_fsync = 0;
		} else {
			fprintf(stderr, "crash_injector: unknown WEAVER_CRASH_ON=%s\n", on);
			return;
		}
	}

	path_filter = getenv("WEAVER_CRASH_PATH");
#ifndef __APPLE__
	real_fsync = (fsync_fn) dlsym(RTLD_NEXT, "fsync");
	real_fcntl = (fcntl_fn) dlsym(RTLD_NEXT, "fcntl");
	real_write = (write_fn) dlsym(RTLD_NEXT, "write");
	real_pwrite = (pwrite_fn) dlsym(RTLD_NEXT, "pwrite");
	real_fdatasync = (fdatasync_fn) dlsym(RTLD_NEXT, "fdatasync");
#endif

	fprintf(stderr, "crash_injector: armed on=%s after=%lu min_bytes=%zu path=%s\n",
			crash_on_fsync && crash_on_write ? "both" :
			crash_on_fsync ? "fsync" : "write",
			v, min_bytes,
			path_filter != NULL && path_filter[0] != '\0' ? path_filter : "*");
	fflush(stderr);

	write_limit = v;
	armed = 1;
}
