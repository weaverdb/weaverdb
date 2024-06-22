
# create the executable
add_library(buffer OBJECT buf_table.c buf_init.c bufmgr.c freelist.c localbuf.c m_lock.c)