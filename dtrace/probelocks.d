#!/usr/sbin/dtrace -s
pid$1::ExecutePlan:entry
{
	ts[tid] = timestamp;
	vts[tid] = vtimestamp;
}
pid$1::ExecutePlan:return
/ts[tid]/
{
	printf("scan time %d %d \n",(timestamp - ts[tid]),(vtimestamp - vts[tid]));
	ts[tid] = 0;
	vts[tid] = 0;
}
plockstat$1:::mutex-block
{
	ustack();
}
plockstat$1:::mutex-spin
{
	ustack();
}
