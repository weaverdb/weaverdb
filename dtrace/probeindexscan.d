#!/usr/sbin/dtrace -s
/*


	Overview probes of Delegated and 
	Inline Index Scans
	MKS  5.28.2006

*/
pid$1:libmtpgjava:DelegatedTransferPointers:entry
{
	self->tp = timestamp;
	@count["avg_pointer_count"] = avg(arg2);
}
pid$1:libmtpgjava:DelegatedTransferPointers:return
/self->tp/
{
	@atp[probefunc] = avg(timestamp - self->tp);
	self->tp = 0;
}
pid$1:libmtpgjava:ExecInitDelegatedIndexScan:entry
{
	self->delegatedscan = timestamp;
	timeon = timestamp;
}
pid$1:libmtpgjava:ExecEndDelegatedIndexScan:return
/self->delegatedscan/
{
	printf("Delegated scan time %d",(timestamp - self->delegatedscan)/1000);
	@atp["DelegatedScan"] = avg(timestamp - self->delegatedscan);
	self->delegatedscan = 0;
	timeon = 0;
}
pid$1:libmtpgjava:ExecInitIndexScan:entry
{
	self->scan = timestamp;
}
pid$1:libmtpgjava:ExecEndIndexScan:return
/self->scan/
{
	printf("Inline scan time %d",(timestamp - self->scan)/1000000);
	@atp["InlineScan"] = avg(timestamp - self->scan);
	self->scan = 0;
}
pid$1:libmtpgjava:HeapTupleSatisfiesSnapshot:entry
/timeon/
{
	self->validity = timestamp;
}
pid$1:libmtpgjava:HeapTupleSatisfiesSnapshot:return
/self->validity/
{
	@val["Qualify"] = sum((timestamp - self->validity));
	self->validity = 0;
}
pid$1:libmtpgjava:DolIndexDelegation:entry
{
	self->delegate = timestamp;
}
pid$1:libmtpgjava:DolIndexDelegation:return
/self->delegate/
{
	@val["Delegate Time"] = sum((timestamp - self->delegate));
	self->delegate = 0;
}

