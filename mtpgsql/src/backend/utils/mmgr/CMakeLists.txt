

add_library(mmgr OBJECT aset.c subset.c mcxt.c portalmem.c)

add_dependencies(mmgr build_fmgr)