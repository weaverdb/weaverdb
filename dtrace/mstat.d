#!/usr/sbin/dtrace -s
plockstat$1:::mutex-acquire
/arg2/
{
	@mutex_spins[arg0] = quantize(arg2);
	@mutex_contention[arg0] = sum(1);
	@mutex_stack[arg0,ustack(4)] = sum(1);
}

