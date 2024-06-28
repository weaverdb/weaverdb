
add_library(utils_hash OBJECT dynahash.c hashfn.c)

add_dependencies(utils_hash build_fmgr)