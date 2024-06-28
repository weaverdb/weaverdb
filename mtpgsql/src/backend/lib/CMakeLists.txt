
# create the executable
add_library(lib OBJECT bit.c fstack.c hasht.c lispsort.c stringinfo.c dllist.c)

add_dependencies(lib build_fmgr)