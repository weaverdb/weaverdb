

add_executable(pg_version 
    pg_version.c     
    $<TARGET_OBJECTS:version>  
)

