
#ifndef _OS_H_
#define _OS_H_
 
#include <pthread.h>
#ifdef SUNOS
#include <sys/sdt.h>
#endif

#define USE_POSIX_TIME
#define HAVE_INT_TIMEZONE
#define NO_EMPTY_STMTS
#define SYSV_DIRENT
#define HAS_TEST_AND_SET
#define SPIN_IS_MUTEX
#define HAVE_ISINF

#define MAX_RANDOM_VALUE (0x7fffffff)

typedef pthread_mutex_t slock_t; 
extern char*	tzname[2];

#ifndef			BIG_ENDIAN
#define			BIG_ENDIAN		4321
#endif
#ifndef			LITTLE_ENDIAN
#define			LITTLE_ENDIAN           1234
#endif
#ifndef			PDP_ENDIAN
#define			PDP_ENDIAN		3412
#endif

#ifndef SUNOS
#define DTRACE_PROBE1(one) ()
#define DTRACE_PROBE2(one,two) ()
#define DTRACE_PROBE3(one,two,three) ()
#define DTRACE_PROBE4(one,two,three,four) ()
#define DTRACE_PROBE5(one,two,three,four,five) ()
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern  void* base_mem_alloc(size_t size);
extern  void base_mem_free(void* pointer);
extern  void* base_mem_realloc(void* pointer, size_t size);

#ifdef __cplusplus
}
#endif

#endif
