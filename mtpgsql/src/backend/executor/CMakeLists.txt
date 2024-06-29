
find_package(JNI REQUIRED)


# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(executor OBJECT ${SRC_FILES})

add_dependencies(executor JNI::JNI fmgrtab)

if (JNI_FOUND)
    message ("JNI found")
    target_include_directories(executor PRIVATE ${JNI_INCLUDE_DIRS})
endif()