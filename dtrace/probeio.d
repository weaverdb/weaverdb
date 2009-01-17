#!/usr/sbin/dtrace -s
/*

	Probe the io
	layer.  MKS  5.28.2006


*/
mtpg$1:::buffer-writebufferio
{
        printf("writeio: db: %d rel: %d blk: %d dirty: %d",arg0,arg1,arg2,arg3);
}
mtpg$1:::buffer-logbufferio
{
        printf("logio: db: %d rel: %d blk: %d dirty: %d",arg0,arg1,arg2,arg3);
}
mtpg$1:::dbwriter-loggedbuffers
{
        printf("log count: %d release: %d",arg0,arg1);
}
mtpg$1:::dbwriter-syncedbuffers
{
        printf("sync count: %d release: %d",arg0,arg1);
}
pid$1:libweaver:FlushAllDirtyBuffers:entry
{
	printf("---flushing buffers---");
}
