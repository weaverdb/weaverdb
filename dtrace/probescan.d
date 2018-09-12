#!/usr/sbin/dtrace -s
/*
   Probe scan times on queries

	MKS  5.28.2006

*/
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
