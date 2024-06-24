

# create the executable
add_library(common OBJECT heaptuple.c indextuple.c indexvalid.c printtup.c scankey.c tupdesc.c)

add_dependencies(common build_fmgr)