
find_package(JNI REQUIRED)

add_library(weaver SHARED 
        WeaverStmtManager.c 
	BaseWeaverConnection.c 
	WeaverValueExtractor.c
)

target_link_libraries(weaver PRIVATE mtpg)

target_compile_definitions(weaver PRIVATE WEAVER_JAVA)

target_include_directories(weaver PRIVATE ${PROJECT_BINARY_DIR}/mtpg/include)

add_dependencies(weaver mtpg)

if (APPLE) 
    target_compile_definitions(weaver PRIVATE LIB_EXTERN=extern)
endif()

if (JNI_FOUND)
    message (STATUS "JNI_INCLUDE_DIRS=${JNI_INCLUDE_DIRS}")
    message (STATUS "JNI_LIBRARIES=${JNI_LIBRARIES}")
    target_include_directories(weaver PRIVATE ${JNI_INCLUDE_DIRS})
endif()

add_custom_command(
        TARGET weaver POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                $<TARGET_FILE:weaver>
                ./)
