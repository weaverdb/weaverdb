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
mtpg$1:::illegalfileaccess
{
	printf("illegal file access %d",arg0);
	ustack();
}
mtpg$1:::count
{
	printf("transfer count %d %d",arg0,arg1);
}
