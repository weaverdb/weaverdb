

find_package(JNI REQUIRED)


# create the executable
add_library(env OBJECT env.c dbwriter.c poolsweep.c freespace.c 
	vacuumlazy.c bitmapset.c 
	pg_crc.c analyze.c dolhelper.c delegatedscan.c 
	WeaverConnection.c FieldTransfer.c connectionutil.c
 )

add_dependencies(env JNI::JNI build_fmgr)

if (JNI_FOUND)
    message (STATUS "JNI_INCLUDE_DIRS=${JNI_INCLUDE_DIRS}")
    message (STATUS "JNI_LIBRARIES=${JNI_LIBRARIES}")
    target_include_directories(env PRIVATE ${JNI_INCLUDE_DIRS})
endif()
if (CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    message("Using ${CMAKE_C_COMPILER_ID} to link library")
    target_compile_definitions(env PRIVATE LIB_EXTERN=extern)
elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_definitions(env PRIVATE LIB_EXTERN=__attribute__((__visibility__("default"))))
endif()