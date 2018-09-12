#!/usr/sbin/dtrace -s
/*

	Probe the io
	layer.  MKS  5.28.2006


*/

mtpg$1:::vacuum-msg
{
        printf("db: %s rel: %s -- %s",copyinstr(arg2),copyinstr(arg1),copyinstr(arg0));
}

mtpg$1:::poolsweep-msg
{
        printf("%s",copyinstr(arg0));
}

mtpg$1:::analyze-msg
{
        printf("db: %s rel: %s -- %s",copyinstr(arg2),copyinstr(arg1),copyinstr(arg0));
}
