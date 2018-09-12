#!/usr/sbin/dtrace -s
mtpg$1:::showcosts
{
	printf("id: %d path type: %d startup: %e total: %e",tid,arg0,*(double*)copyin(arg1,8),*(double*)copyin(arg2,8));	
}
pid$1::PGParsingFunc:entry
{
        printf("id: %d %s",tid,copyinstr(arg1));
}
