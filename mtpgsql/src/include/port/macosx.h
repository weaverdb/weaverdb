
#ifndef _OS_H_
#define _OS_H_

#include <pthread.h>

#define NO_EMPTY_STMTS
#define SYSV_DIRENT
#define HAS_TEST_AND_SET
#define SPIN_IS_MUTEX
#define USE_POSIX_TIME
#define HAVE_TM_ZONE
#define HAVE_ISINF
#define FRIENDLY_DBWRITER

#define MAX_RANDOM_VALUE (0x7ffffffe)

extern char*	tzname[2];
typedef pthread_mutex_t slock_t; 

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
