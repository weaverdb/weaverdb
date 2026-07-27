

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

target_include_directories(env PRIVATE ../access/pgvector)

message("Using ${CMAKE_C_COMPILER_ID} to link library")
# Export visibility is controlled by Mach-O export lists / ELF version scripts
# on the shared library targets; keep declarations as plain extern here.
target_compile_definitions(env PRIVATE LIB_EXTERN=extern)

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_compile_definitions(env PRIVATE ANDROID NOCUSERID)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_compile_definitions(env PRIVATE MACOSX NOCUSERID)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Embeddable Linux build: avoid deprecated cuserid().
    target_compile_definitions(env PRIVATE LINUX NOCUSERID)
endif()