
find_package(JNI REQUIRED)


# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(executor OBJECT ${SRC_FILES})

add_dependencies(executor JNI::JNI)

if (JNI_FOUND)
    message (STATUS "JNI_INCLUDE_DIRS=${JNI_INCLUDE_DIRS}")
    message (STATUS "JNI_LIBRARIES=${JNI_LIBRARIES}")
    target_include_directories(executor PRIVATE ${JNI_INCLUDE_DIRS})
endif()