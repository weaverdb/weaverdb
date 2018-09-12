#!/usr/sbin/dtrace -s
pid$1::BufTableDelete:entry
{
	ustack();
}
