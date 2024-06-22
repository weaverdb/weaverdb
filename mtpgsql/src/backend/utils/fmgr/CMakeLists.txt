
find_package(JNI REQUIRED)

add_library(fmgr OBJECT fmgr.c)

add_dependencies(fmgr JNI::JNI)

if (JNI_FOUND)
    message (STATUS "JNI_INCLUDE_DIRS=${JNI_INCLUDE_DIRS}")
    message (STATUS "JNI_LIBRARIES=${JNI_LIBRARIES}")
    target_include_directories(fmgr PRIVATE ${JNI_INCLUDE_DIRS})
endif()
