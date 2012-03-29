#!/usr/sbin/dtrace -s
/*

	Probe the io
	layer.  MKS  5.28.2006


*/
mtpg$1:::dbwriter-logged
{
        printf("logged: %d",arg0);
}
mtpg$1:::dbwriter-loggedbuffers
{
        printf("log count: %d release: %d freecount: %d",arg0,arg1,arg2);
}
mtpg$1:::dbwriter-syncedbuffers
{
        printf("sync count: %d release: %d frecount: %d forcommit: %d",arg0,arg1,arg2,arg3);
}
pid$1:libweaver:FlushAllDirtyBuffers:entry
{
	printf("---flushing buffers---");
}

