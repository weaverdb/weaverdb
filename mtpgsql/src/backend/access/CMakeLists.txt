
add_subdirectory(common)
add_subdirectory(hash)
add_subdirectory(heap)
add_subdirectory(index)
add_subdirectory(nbtree)
add_subdirectory(transam)

add_library(access OBJECT 
    $<TARGET_OBJECTS:common> 
    $<TARGET_OBJECTS:hash>
    $<TARGET_OBJECTS:heap>
    $<TARGET_OBJECTS:index>
    $<TARGET_OBJECTS:nbtree>
    $<TARGET_OBJECTS:transam>
)

add_dependencies(access build_fmgr)