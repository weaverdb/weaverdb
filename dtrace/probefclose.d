#!/usr/sbin/dtrace -s
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
