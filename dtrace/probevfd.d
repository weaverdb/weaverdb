#!/usr/sbin/dtrace -s
/*

	Probe the virtual file interface
	layer.  MKS  5.28.2006


*/
pid$1::FileClose:entry
{
	printf("FileClose: %d",arg0);
}
pid$1::fileNameOpenFile:return
{
        printf("FileOpen: %d",arg1);
}
pid$1::AllocateVfd:return
{
        printf("Allocated: %d",arg1);
}
pid$1::FreeVfd:entry
{
        printf("Freed: %d",arg0);
}
mtpg$1:::file-retired
{
        printf("retired: %s fds: %d",copyinstr(arg1),arg0);
}
mtpg$1:::file-activated
{
        printf("activated: %s fds: %d",copyinstr(arg1),arg0);
}
