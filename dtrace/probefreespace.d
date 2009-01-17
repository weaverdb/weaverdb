#!/usr/sbin/dtrace -s
/*

	Probe the freespace tracking
	layer.  MKS  5.28.2006


*/
pid$1::GetFreespace:entry
{
	printf("%d req:%d lim:%d",arg0,arg1,arg2);
}
pid$1::GetFreespace:return
{
        printf("return: %d",arg1);
}
mtpg$1:::freespace-reservation
{
	printf("reservation: %s %d %d %d",copyinstr(arg0),arg1,arg2,arg3);
}
