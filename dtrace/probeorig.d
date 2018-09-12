#!/usr/sbin/dtrace -s
pid$1::ExecInitSeqScan:entry
{
	ts = timestamp;
	timeon = 1;
}
pid$1::ExecEndSeqScan:return
/ts/
{
	printf("scan time %d",(timestamp - ts) /1000000) ;
	@val["PlanTime"] = sum(timestamp - ts);
	ts = 0;
	timeon = 0;
}
pid$1::HeapTupleSatisfiesSnapshot:entry
/timeon/
{
        validity = timestamp;
}
pid$1::HeapTupleSatisfiesSnapshot:return
/validity/
{
        @val["Qualify"] = sum(timestamp - validity);
        validity = 0;
}
pid$1::smgrread:entry
/timeon/
{
        rt= timestamp;
}
pid$1::smgrread:return
/rt/
{
        @val["IO"] = sum(timestamp - rt);
        rt = 0;
}

