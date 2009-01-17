#!/usr/sbin/dtrace -s
plockstat$1:::mutex-block
{
        @mtxblock[arg0] = sum(1);
}
plockstat$1:::mutex-spin
{
        @mtxspin[arg0] = sum(1);
}

