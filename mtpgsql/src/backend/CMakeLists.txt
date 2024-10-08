
add_subdirectory(access)
add_subdirectory(bootstrap)
add_subdirectory(catalog)
add_subdirectory(commands)
add_subdirectory(env)
add_subdirectory(executor)
add_subdirectory(lib)
add_subdirectory(libpq)
add_subdirectory(main)
add_subdirectory(nodes)
add_subdirectory(optimizer)
add_subdirectory(parser)
add_subdirectory(postmaster)
add_subdirectory(regex)
add_subdirectory(rewrite)
add_subdirectory(storage)
add_subdirectory(tcop)
add_subdirectory(utils)

add_library(mtpg STATIC 
    ##  access ##
    $<TARGET_OBJECTS:common> 
    $<TARGET_OBJECTS:hash>
    $<TARGET_OBJECTS:heap>
    $<TARGET_OBJECTS:index>
    $<TARGET_OBJECTS:nbtree>
    $<TARGET_OBJECTS:transam>

    $<TARGET_OBJECTS:catalog>
    $<TARGET_OBJECTS:commands>
    $<TARGET_OBJECTS:env>
    $<TARGET_OBJECTS:executor>
    $<TARGET_OBJECTS:lib>
    $<TARGET_OBJECTS:libpq>
    $<TARGET_OBJECTS:nodes>
    ##  optimizer ##
    $<TARGET_OBJECTS:path> 
    $<TARGET_OBJECTS:plan>
    $<TARGET_OBJECTS:prep>
    $<TARGET_OBJECTS:util>

    $<TARGET_OBJECTS:parser>
    $<TARGET_OBJECTS:regex>
    $<TARGET_OBJECTS:rewrite>
    ##  storage ##
    $<TARGET_OBJECTS:buffer> 
    $<TARGET_OBJECTS:file>
    $<TARGET_OBJECTS:ipc>
    $<TARGET_OBJECTS:lmgr>
    $<TARGET_OBJECTS:page>
    $<TARGET_OBJECTS:smgr>

    $<TARGET_OBJECTS:tcop>
    ##  utils ##
    $<TARGET_OBJECTS:adt> 
    $<TARGET_OBJECTS:cache>
    $<TARGET_OBJECTS:error>
    $<TARGET_OBJECTS:fmgr>
    $<TARGET_OBJECTS:utils_hash>
    $<TARGET_OBJECTS:init>
    $<TARGET_OBJECTS:lzf>
    $<TARGET_OBJECTS:misc>
    $<TARGET_OBJECTS:mmgr>
    $<TARGET_OBJECTS:sort>
    $<TARGET_OBJECTS:time>

    $<TARGET_OBJECTS:fmgrtab>  
    $<TARGET_OBJECTS:version>  
)

#add_library(rweaver SHARED empty.c)

#target_link_libraries(rweaver PRIVATE mtpg)

#message("Using ${CMAKE_C_COMPILER_ID} to link library")
#if (CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
#    target_link_options(rweaver PRIVATE -fvisibility=hidden -exported_symbols_list ${CMAKE_CURRENT_SOURCE_DIR}/clang_mapfile.txt)
#elseif (CMAKE_C_COMPILER_ID STREQUAL "Clang")
#    target_link_options(rweaver PRIVATE -fvisibility=hidden -exported_symbols_list ${CMAKE_CURRENT_SOURCE_DIR}/clang_mapfile.txt)
#elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
#    target_link_options(rweaver PRIVATE -M${CMAKE_CURRENT_SOURCE_DIR}/mapfile.txt)
#endif()

target_include_directories(mtpg INTERFACE ../include/interface)

add_executable(postgres 
    $<TARGET_OBJECTS:main> 
    $<TARGET_OBJECTS:bootstrap> 
    $<TARGET_OBJECTS:postmaster>
)

target_link_libraries(postgres PRIVATE mtpg)

add_custom_command(
    TARGET mtpg POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/mtpg/include/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/../include/interface/WeaverInterface.h ${PROJECT_BINARY_DIR}/mtpg/include/
    COMMAND ${CMAKE_COMMAND} -E copy  ${CMAKE_CURRENT_SOURCE_DIR}/../include/interface/connectionutil.h ${PROJECT_BINARY_DIR}/mtpg/include/
    BYPRODUCTS ${PROJECT_BINARY_DIR}/mtpg/include/WeaverInterface.h ${PROJECT_BINARY_DIR}/mtpg/include/connectionutil.h
)

set_target_properties(mtpg PROPERTIES 
    PUBLIC_HEADER ../include/env/WeaverInterface.h ../include/env/connectionutil.h
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME "mtpg"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
)


