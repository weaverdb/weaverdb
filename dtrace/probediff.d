#!/usr/sbin/dtrace -s
pid$1:libmtpgjava:DelegatedTransferPointers:entry
{
	self->tp = timestamp;
}
pid$1:libmtpgjava:DelegatedTransferPointers:return
/self->tp/
{
	@atp[probefunc] = avg(timestamp - self->tp);
	@count["avg_pointer_count"] = avg(arg2);
	self->tp = 0;
}
pid$1:libmtpgjava:ExecDelegatedSeqScan:entry
{
	self->delegatedseqscan = timestamp;
	timeon = timestamp;
}
pid$1:libmtpgjava:ExecEndDelegatedSeqScan:return
/self->delegatedseqscan/
{
	printf("Delegated seq scan time %d",(timestamp - self->delegatedseqscan)/1000000);
	@atp["DelegatedSeqScan"] = avg(timestamp - self->delegatedseqscan);
	self->delegatedseqscan = 0;
	timeon = 0;
}
pid$1:libmtpgjava:ExecInitSeqScan:entry
{
	self->scan = timestamp;
}
pid$1:libmtpgjava:ExecEndSeqScan:return
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
pid$1:libmtpgjava:DelegatedHeapScan:entry
{
	self->delegate = timestamp;
}
pid$1:libmtpgjava:DelegatedHeapScan:return
/self->delegate/
{
	@val["Delegate Time"] = sum((timestamp - self->delegate));
	self->delegate = 0;
}

