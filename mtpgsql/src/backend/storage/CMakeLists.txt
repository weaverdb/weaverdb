
add_subdirectory(buffer)
add_subdirectory(file)
add_subdirectory(ipc)
add_subdirectory(lmgr)
add_subdirectory(page)
add_subdirectory(smgr)

add_library(storage OBJECT 
    $<TARGET_OBJECTS:buffer> 
    $<TARGET_OBJECTS:file>
    $<TARGET_OBJECTS:ipc>
    $<TARGET_OBJECTS:lmgr>
    $<TARGET_OBJECTS:page>
    $<TARGET_OBJECTS:smgr>
)