#!/usr/sbin/dtrace -s
pid$1:libmtpgjava:btbuild:entry
{
	ts[tid] = timestamp;
}
pid$1:libmtpgjava:btbuild:return
/ts[tid]/
{
	printf("btbuild time %d for thread %d \n",(timestamp - ts[tid]) /1000000 ,tid);
	ts[tid] = 0;
}
pid$1:libmtpgjava:DelegatedIndexBuild:entry
{
	d = timestamp;
}
pid$1:libmtpgjava:DelegatedIndexBuild:return
/d/
{
	printf("delegated time %d for thread %d \n",(timestamp - d) /1000000 ,tid);
	d = 0;
}
