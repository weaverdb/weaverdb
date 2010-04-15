
#ifndef _OS_H_
#define _OS_H_
 
#include <pthread.h>

#define USE_POSIX_TIME
#define HAVE_INT_TIMEZONE
#define NO_EMPTY_STMTS
#define SYSV_DIRENT
#define HAS_TEST_AND_SET
#define SPIN_IS_MUTEX
#define HAVE_ISINF

#ifndef SUNOS
#define SUNOS
#endif

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
#ifndef			BYTE_ORDER
#define			BYTE_ORDER		LITTLE_ENDIAN
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
