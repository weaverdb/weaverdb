
find_package(JNI REQUIRED)

add_library(fmgr OBJECT fmgr.c)

add_dependencies(fmgr JNI::JNI)

add_dependencies(fmgr fmgrtab)

if (JNI_FOUND)
    message ("JNI found")
    target_include_directories(fmgr PRIVATE ${JNI_INCLUDE_DIRS})
endif()
