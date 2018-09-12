#!/usr/sbin/dtrace -s
/*
    Probes the transfer of ItemPointers from 
	the Delegated Helper Thread to the 
	main thread   MKS 5.28.2006

*/
pid$1:libmtpgjava:DelegatedTransferPointers:entry
{
	printf("pointer count: %d \n",arg2);
	@count["avg_pointer_count"] = avg(arg2);
	self->tp = timestamp;
}
pid$1:libmtpgjava:DelegatedTransferPointers:return
/self->tp/
{
	@atp[probefunc] = avg(timestamp - self->tp);
	self->tp = 0;
}
pid$1:libmtpgjava:CollectPointers:entry
{
	self->cp = timestamp;
}
pid$1:libmtpgjava:CollectPointers:return
/self->cp/
{
	@atp[probefunc] = avg(timestamp - self->cp);
}
