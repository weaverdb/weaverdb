#!/usr/sbin/dtrace -s
plockstat$1:::mutex-block
{
        self->mtxblock[arg0] = timestamp;
}
plockstat$1:::mutex-spin
{
        self->mtxspin[arg0] = timestamp;
}
plockstat$1:::mutex-block
/self->mtxspin[arg0]/
{
        @mtx_vain_spin[arg0, ustack(5)] =
            quantize(timestamp - self->mtxspin[arg0]);
        self->mtxspin[arg0] = 0;
}
plockstat$1:::mutex-acquire
/self->mtxblock[arg0]/
{
        @mtx_block[arg0, ustack(5)] =
            avg(timestamp - self->mtxblock[arg0]);
        self->mtxblock[arg0] = 0;
}
plockstat$1:::mutex-acquire
/self->mtxspin[arg0]/
{
        @mtx_spin[arg0, ustack(5)] =
            quantize(timestamp - self->mtxspin[arg0]);
        self->mtxspin[arg0] = 0;
}
plockstat$1:::mutex-error
/self->mtxblock[arg0]/
{
        self->mtxblock[arg0] = 0;
}
plockstat$1:::mutex-error
/self->mtxspin[arg0]/
{
        self->mtxspin[arg0] = 0;
}

