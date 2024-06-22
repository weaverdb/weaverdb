
add_subdirectory(path)
add_subdirectory(plan)
add_subdirectory(prep)
add_subdirectory(util)

add_library(optimizer OBJECT 
    $<TARGET_OBJECTS:path> 
    $<TARGET_OBJECTS:plan>
    $<TARGET_OBJECTS:prep>
    $<TARGET_OBJECTS:util>
)