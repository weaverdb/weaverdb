
find_package(JNI REQUIRED)

add_library(weaver_jni SHARED 
        WeaverStmtManager.c 
	BaseWeaverConnection.c 
	WeaverValueExtractor.c
)

target_link_libraries(weaver_jni PRIVATE weaver)

target_compile_definitions(weaver_jni PRIVATE WEAVER_JAVA)


message("Using ${CMAKE_C_COMPILER_ID} to link library")
if (CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    target_compile_definitions(weaver_jni PRIVATE LIB_EXTERN=extern)
    target_link_options(weaver_jni PRIVATE -fvisibility=hidden -exported_symbols_list ${CMAKE_CURRENT_SOURCE_DIR}/clang_mapfile.txt)
elseif (CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_definitions(weaver_jni PRIVATE LIB_EXTERN=extern)
    target_link_options(weaver_jni PRIVATE -fvisibility=hidden -exported_symbols_list ${CMAKE_CURRENT_SOURCE_DIR}/clang_mapfile.txt)
elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_definitions(weaver_jni PRIVATE LIB_EXTERN=__attribute__((__visibility__("default"))))
    target_link_options(weaver_jni PRIVATE -M${CMAKE_CURRENT_SOURCE_DIR}/mapfile.txt)
endif()

if (JNI_FOUND)
    message (STATUS "JNI_INCLUDE_DIRS=${JNI_INCLUDE_DIRS}")
    message (STATUS "JNI_LIBRARIES=${JNI_LIBRARIES}")
    target_include_directories(weaver_jni PRIVATE ${JNI_INCLUDE_DIRS})
endif()

#add_custom_command(
#        TARGET weaver_jni POST_BUILD
#        COMMAND ${CMAKE_COMMAND} -E copy
#                $<TARGET_FILE:weaver_jni>
#                ${CMAKE_CURRENT_SOURCE_DIR}/)

#set_target_properties(weaver PROPERTIES 
#    PUBLIC_HEADER ../include/env/WeaverInterface.h ../include/env/connectionutil.h
#    VERSION ${PROJECT_VERSION}
#    SOVERSION 1
#    OUTPUT_NAME "weaver"
#    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
#)
