
# create the executable
add_library(lmgr OBJECT lmgr.c lock.c multithread.c)

add_dependencies(lmgr fmgrtab)