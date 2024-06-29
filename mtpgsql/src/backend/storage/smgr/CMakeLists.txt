
# create the executable
add_library(smgr OBJECT smgr.c smgrtype.c vfd.c)

add_dependencies(smgr fmgrtab)