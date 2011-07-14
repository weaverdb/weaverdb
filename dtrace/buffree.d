#!/usr/sbin/dtrace -s
/*

	Probe the io
	layer.  MKS  5.28.2006


*/
/*
mtpg$1:::buffer-evict
{
        printf("evicted: db: %s rel: %s block: %d",copyinstr(arg0),copyinstr(arg1),arg2);
}
mtpg$1:::buffer-store
{
        printf("store: db: %s rel: %s block: %d",copyinstr(arg0),copyinstr(arg1),arg2);
}
*/
mtpg$1:::buffer-replace
{
        printf("replace: db: %s rel: %s blk: %d rel: %s blk: %d",copyinstr(arg0),copyinstr(arg1),arg2,copyinstr(arg3),arg4);
}

