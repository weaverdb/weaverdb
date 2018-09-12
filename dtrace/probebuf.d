#!/usr/sbin/dtrace -s
/*
  Probe buffer cache actions
	MKS	5.28.2006


*/
pid$1::BufferHit:entry
{
        @hits[copyinstr(arg3)] = sum(1);
}
pid$1::BufferMiss:entry
{
        @misses[copyinstr(arg2)] = sum(1);
}
pid$1::BufferReplaceMiss:entry
{
        @rmisses[copyinstr(arg2)] = sum(1);
}
mtpg$1:::buffer-freesteal
{
        printf("kind: %c split %d",(char)arg0,arg1);
}
pid$1::LockedHashSearch:entry
/(arg3==2)/
{
	@remove[(char)arg0] = sum(1);
}
dtrace:::END
{
        printf("hits");
        printa(@hits);
        printf("misses");
        printa(@misses);
        printf("rmisses");
        printa(@rmisses);
	printf("removes");
	printa(@remove);
}

