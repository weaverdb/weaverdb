/*-------------------------------------------------------------------------
 *
 * main.c
 *	  Stub main() routine for the postgres backend.
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */
#include <unistd.h>


#if defined(__alpha__) && !defined(linux)
#include <sys/sysinfo.h>
#include "machine/hal_sysinfo.h"
#define ASSEMBLER
#include <sys/proc.h>
#undef ASSEMBLER
#endif

#include "postgres.h"
#include "env/env.h"
#include "storage/smgr.h"
#ifdef USE_LOCALE
#include <locale.h>
#endif
#include "miscadmin.h"
#include "bootstrap/bootstrap.h"
#include "tcop/tcopprot.h"

#define NOROOTEXEC "\
\n\"root\" execution of the PostgreSQL backend is not permitted.\n\n\
The backend must be started under its own userid to prevent\n\
a possible system security compromise. See the INSTALL file for\n\
more information on how to properly start the postmaster.\n\n"

int
main(int argc, char *argv[])
{
	int			len;

#if defined(__alpha__)
#ifdef NOFIXADE
	int			buffer[] = {SSIN_UACPROC, UAC_SIGBUS};

#endif	 /* NOFIXADE */
#ifdef NOPRINTADE
	int			buffer[] = {SSIN_UACPROC, UAC_NOPRINT};

#endif	 /* NOPRINTADE */
#endif

#ifdef USE_LOCALE
	setlocale(LC_CTYPE, "");	/* take locale information from an
								 * environment */
	setlocale(LC_COLLATE, "");
	setlocale(LC_MONETARY, "");
#endif
#if defined(NOFIXADE) || defined(NOPRINTADE)

	/*
	 * Must be first so that the bootstrap code calls it, too. (Only
	 * needed on some RISC architectures.)
	 */

#if defined(ultrix4)
	syscall(SYS_sysmips, MIPS_FIXADE, 0, NULL, NULL, NULL);
#endif

#if defined(__alpha__)
	if (setsysinfo(SSI_NVPAIRS, buffer, 1, (caddr_t) NULL,
				   (unsigned long) NULL) < 0)
		elog(NOTICE, "setsysinfo failed: %d\n", errno);
#endif

#endif	 /* NOFIXADE || NOPRINTADE */

	/*
	 * use one executable for both postgres and postmaster, invoke one or
	 * the other depending on the name of the executable
	 */

	len = strlen(argv[0]);
	if (!geteuid())
	{
		fprintf(stderr, "%s", NOROOTEXEC);
		exit(1);
	}
        
        {
        int post = 0;
	if (post || (len >= 10 && !strcmp(argv[0] + len - 10, "postmaster"))) {
		exit(PostmasterMain(argc, argv));
        }
        }
	/*
	 * if the first argument is "-boot", then invoke the backend in
	 * bootstrap mode
	 */
#ifdef ZFS
        if (argc > 1 && strcmp(argv[1], "-zfstest") == 0)
		exit(zfstest(argv[2]));
        
	else 
#endif
        if (argc > 1 && strcmp(argv[1], "-boot") == 0)
		exit(BootstrapMain(argc - 1, argv + 1));		/* remove the -boot arg
														 * from the command line */
	else
		exit(PostgresMain(argc, argv, argc, argv));
}
