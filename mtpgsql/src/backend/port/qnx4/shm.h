/*-------------------------------------------------------------------------
 *
 * shm.h
 *	  System V Shared Memory Emulation
 *
 * Copyright (c) 1999, repas AEG Automation GmbH
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/port/qnx4/shm.h,v 1.1.1.1 2006/08/12 00:21:15 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#ifndef _SYS_SHM_H
#define _SYS_SHM_H

#include <sys/ipc.h>

#ifdef	__cplusplus
extern		"C"
{
#endif

#define SHM_R	0400			/* read permission */
#define SHM_W	0200			/* write permission */

	struct shmid_ds
	{
		int			dummy;
	};

	extern void *shmat(int shmid, const void *shmaddr, int shmflg);
	extern int	shmdt(const void *addr);
	extern int	shmctl(int shmid, int cmd, struct shmid_ds * buf);
	extern int	shmget(key_t key, size_t size, int flags);

#ifdef	__cplusplus
}

#endif

#endif	 /* _SYS_SHM_H */
