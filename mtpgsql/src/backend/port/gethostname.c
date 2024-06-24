/* $Id: gethostname.c,v 1.1.1.1 2006/08/12 00:21:13 synmscott Exp $ */




#include <sys/utsname.h>

#include "config.h"

int
gethostname(char *name, int namelen)
{
	static struct utsname mname;
	static int	called = 0;

	if (!called)
	{
		called++;
		uname(&mname);
	}
	strncpy(name, mname.nodename, (SYS_NMLN < namelen ? SYS_NMLN : namelen));

	return 0;
}
