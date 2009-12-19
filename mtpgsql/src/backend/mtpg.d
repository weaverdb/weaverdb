
provider mtpg {
	probe count(int, int);
	probe wait(int, int);
	probe run(int, int);
	probe showcosts(int, int, int);
	probe indexcost(int, int, int, int);
	probe initplan(int);
        probe freespace__miss(int,int);
        probe freespace__hit(int,int);
        probe freespace__reservation(string,int,int,int);
        probe searches(int);
        probe buffer__tailmiss(int,int,int,int);
        probe buffer__doublefree(int);
        probe buffer__freesteal(int,int);
        probe buffer__miss(int, int, string);
        probe buffer__hit(int, int, int, string);
        probe buffer__replacemiss(int, int, string);
        probe buffer__pinmiss(int, int, string);
        probe buffer__pininvalid(int, int, string);
        probe buffer__writelockdefer(int, int);
        probe buffer__cxtnotpassed();
        probe buffer__waitbufferio(int,int,int,int);
        probe buffer__inboundbufferio(int,int,int,int);
        probe buffer__readbufferio(int,int,int,int);
        probe buffer__logbufferio(int,int,int,int);
        probe buffer__writebufferio(int,int,int,int);
        probe file__opened(int,string);
        probe file__closed(int,string);
        probe file__retired(int, string, int);
        probe file__activated(int, string, int);
        probe file__maxcheck(int, int, int);
        probe file__search(int, string, char, int);
        probe file__drop(int, string, char, int);
        probe file__poolsize(int);
	probe dbwriter__logged(int);
	probe dbwriter__softcommit(long);
	probe dbwriter__commit(long);
	probe dbwriter__loggedbuffers(int,int);
	probe dbwriter__syncedbuffers(int,int);
	probe dbwriter__circularflush(int,int);
	probe dbwriter__tolerance(string,string,double*,double*);
	probe dbwriter__accesses(string,string,double*,double*);
	probe dbwriter__vacuumactivation(string,string,long);
        probe dbwriter__indexdirty(string,long);
	probe vacuum__msg(string,long,long);  /* fmt,relation,database */
	probe blob__msg(string,long,long);  /* fmt,relation,database */
	probe poolsweep__msg(string,long,long);  /* fmt,relid */
	probe analyze__msg(string,long,long);  /* fmt,relid */
	probe env__msg(int,string);  
        probe freespace__msg(string,long,long);

};
