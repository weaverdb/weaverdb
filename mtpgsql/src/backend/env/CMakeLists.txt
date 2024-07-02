

find_package(JNI REQUIRED)

get_property(BUILDTIME GLOBAL PROPERTY BUILDTIME)

# create the executable
add_library(env OBJECT env.c dbwriter.c poolsweep.c freespace.c 
	vacuumlazy.c bitmapset.c 
	pg_crc.c analyze.c dolhelper.c delegatedscan.c 
	WeaverConnection.c FieldTransfer.c connectionutil.c
 )

add_dependencies(env JNI::JNI fmgrtab)

target_compile_definitions(env PRIVATE BUILDTIME=${BUILDTIME})

if (JNI_FOUND)
    message (STATUS "JNI found")
    target_include_directories(env PRIVATE ${JNI_INCLUDE_DIRS})
endif()

message("Using ${CMAKE_C_COMPILER_ID} to link library")
if (CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    target_compile_definitions(env PRIVATE LIB_EXTERN=extern)
elseif (CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_definitions(env PRIVATE LIB_EXTERN=extern)
elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_definitions(env PRIVATE LIB_EXTERN=__attribute__((__visibility__("default"))))
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_compile_definitions(env PRIVATE ANDROID NOCUSERID)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_compile_definitions(env PRIVATE MACOSX NOCUSERID)
endif()