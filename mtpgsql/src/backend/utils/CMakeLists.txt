
get_property(BKIOPTS GLOBAL PROPERTY BKIOPTS)

message(WARNING ${CMAKE_CURRENT_SOURCE_DIR})

add_custom_target(build_fmgr ALL
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/../../include/catalog/pg_proc.h ${CMAKE_CURRENT_BINARY_DIR}/pg_proc.hpp
  COMMAND env CPP=${CMAKE_CXX_COMPILER} sh ${SHOPTS} Gen_fmgrtab.sh ${BKIOPTS} ${CMAKE_CURRENT_BINARY_DIR}/pg_proc.hpp
  COMMAND ${CMAKE_COMMAND} -E rename fmgr.h ../../include/fmgr.h
  BYPRODUCTS ${CMAKE_CURRENT_SOURCE_DIR}/../../include/fmgr.h ${CMAKE_CURRENT_SOURCE_DIR}/fmgrtab.c
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM
)

add_subdirectory(adt)
add_subdirectory(cache)
add_subdirectory(error)
add_subdirectory(fmgr)
add_subdirectory(hash)
add_subdirectory(init)
add_subdirectory(lzf)
add_subdirectory(misc)
add_subdirectory(mmgr)
add_subdirectory(sort)
add_subdirectory(time)

add_library(fmgrtab OBJECT 
    fmgrtab.c
)

add_library(version OBJECT 
    version.c
)

target_compile_options(fmgrtab PRIVATE -Wno-constant-conversion)

add_dependencies(time build_fmgr)
