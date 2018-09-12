#!/usr/sbin/dtrace -s
pid$1:libmtpgjava::entry
{
	self->cp[probefunc] = timestamp;
}
pid$1:libmtpgjava::return
/self->cp[probefunc]/
{
	@atp[probefunc] = avg(timestamp - self->cp[probefunc]);
}
