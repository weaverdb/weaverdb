
#ifndef _OS_H_
#define _OS_H_


#include <pthread.h>



#define FASTBUILD_DEBUG
#define NO_EMPTY_STMTS
#define SYSV_DIRENT
#define HAS_TEST_AND_SET
#define SPIN_IS_MUTEX
#define USE_POSIX_TIME
#define HAVE_INT_TIMEZONE
#define HAVE_ISINF



#ifndef SUNOS
#define SUNOS

typedef pthread_mutex_t slock_t; 
extern char*	tzname[2];
#endif

#ifndef			BIG_ENDIAN
#define			BIG_ENDIAN		4321
#endif
#ifndef			LITTLE_ENDIAN
#define			LITTLE_ENDIAN	1234
#endif
#ifndef			PDP_ENDIAN
#define			PDP_ENDIAN		3412
#endif
#ifndef			BYTE_ORDER
#define			BYTE_ORDER		BIG_ENDIAN
#endif

#endif

