#!/usr/sbin/dtrace -s
pid$1::ExecInitSeqScan:entry
{
	ts = timestamp;
	vts = vtimestamp;
	timeon = 1;
}
pid$1::ExecEndSeqScan:return
/ts/
{
	printf("scan time %d",(timestamp - ts) /1000000) ;
	@val["ExecSeqScan"] = sum(timestamp - ts);
	@val["Virtualized - ExecSeqScan"] = sum(vtimestamp - vts);
	ts = 0;
	vts = 0;
	timeon = 0;
}
pid$1::HeapTupleSatisfiesSnapshot:entry
/timeon/
{
        validity = timestamp;
	vvalidity = vtimestamp;
}
pid$1::HeapTupleSatisfiesSnapshot:return
/validity/
{
        @val["HeapTupleSatisfiesSnapshot"] = sum(timestamp - validity);
        @val["Virtualized - HeapTupleSatisfiesSnapshot"] = sum(vtimestamp - vvalidity);
        @val["HeapTupleSatisfiesSnapshot - Call Count"] = sum(1);
        validity = 0;
	vvalidity = 0;
}
pid$1::smgrread:entry
/timeon/
{
        rt= timestamp;
        vrt= vtimestamp;
}
pid$1::smgrread:return
/rt/
{
        @val["smgrread"] = sum(timestamp - rt);
        @val["Virtualized - smgrread"] = sum(vtimestamp - vrt);
        @val["smgread - Call Count"] = sum(1);
        rt = 0;
        vrt = 0;
}
pid$1::ReadBuffer:entry
/timeon/
{
        brt= timestamp;
        vbrt= vtimestamp;
}
pid$1::ReadBuffer:return
/brt/
{
        @val["ReadBuffer"] = sum(timestamp - brt);
        @val["Virtualized - ReadBuffer"] = sum(vtimestamp - vbrt);
        brt = 0;
        vbrt = 0;
}
pid$1::ExecutePlan:entry
{
        pt= timestamp;
        vpt= vtimestamp;
}
pid$1::ExecutePlan:return
/pt/
{
        @val["ExecutePlan"] = sum(timestamp - pt);
        @val["Virtualized - ExecutePlan"] = sum(vtimestamp - vpt);
        pt = 0;
        vpt = 0;
}

