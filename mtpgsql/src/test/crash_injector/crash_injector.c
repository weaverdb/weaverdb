/*-------------------------------------------------------------------------
 *
 * crash_injector.c
 *	  Test-only LD_PRELOAD / DYLD_INSERT_LIBRARIES helper.
 *
 * Interposes write(2)/pwrite(2) and raises SIGKILL after N large writes.
 * This is NOT linked into libweaver, libweaver_jni, or postgres. Crash
 * injection stays outside production code.
 *
 * Env:
 *	WEAVER_CRASH_AFTER_WRITES	kill after this many counted writes (>0)
 *	WEAVER_CRASH_MIN_BYTES		only count writes of at least this size
 *				(default 4096, so relation page flushes)
 *	WEAVER_CRASH_GATE		if set, do not count writes until this
 *				path exists (so initweaverbackend is not
 *				the crash target)
 *
 *-------------------------------------------------------------------------
 */

#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
#define CRASH_INTERPOSE(_replacement, _replacee) \
	__attribute__((used)) static struct { \
		const void *replacement; \
		const void *replacee; \
	} _interpose_##_replacee \
	__attribute__((section("__DATA,__interpose"))) = { \
		(const void *)&_replacement, \
		(const void *)&_replacee \
	};

/* Bind the real kernel traps, not the interposed write() symbol. */
extern ssize_t darwin_write_nocancel(int, const void *, size_t)
	__asm__("_write$NOCANCEL");
extern ssize_t darwin_pwrite_nocancel(int, const void *, size_t, off_t)
	__asm__("_pwrite$NOCANCEL");
#else
#include <dlfcn.h>
typedef ssize_t (*write_fn)(int, const void *, size_t);
typedef ssize_t (*pwrite_fn)(int, const void *, size_t, off_t);
static write_fn real_write;
static pwrite_fn real_pwrite;
#endif

static volatile unsigned long write_count;
static unsigned long write_limit;
static size_t min_bytes = 4096;
static int armed;

static int
gate_open(void)
{
	const char *gate = getenv("WEAVER_CRASH_GATE");

	if (gate == NULL || gate[0] == '\0')
		return 1;
	return access(gate, F_OK) == 0;
}

static void
maybe_crash(ssize_t nbyte)
{
	unsigned long n;

	if (!armed || !gate_open() || nbyte < 0 || (size_t) nbyte < min_bytes)
		return;

	n = __sync_add_and_fetch(&write_count, 1);
	if (n >= write_limit)
		kill(getpid(), SIGKILL);
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
	maybe_crash(r);
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
	maybe_crash(r);
	return r;
}

#ifdef __APPLE__
CRASH_INTERPOSE(crash_write, write)
CRASH_INTERPOSE(crash_pwrite, pwrite)
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
#endif

__attribute__((constructor))
static void
crash_injector_ctor(void)
{
	const char *lim;
	const char *minb;
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

	write_limit = v;
	armed = 1;
}
