# Test-only embed SQL driver. Links libweaver so crash/recover goes through
# initweaverbackend / GoMultiuser (shadow log). Not a production tool.

if (NOT DEFINED BLCKSZ)
	set(BLCKSZ 8192)
endif()

add_executable(weaver_embed_sql weaver_embed_sql.c)

target_link_libraries(weaver_embed_sql PRIVATE weaver Threads::Threads)

target_compile_definitions(weaver_embed_sql PRIVATE
	LIB_EXTERN=
	BLCKSZ=${BLCKSZ}
)

target_include_directories(weaver_embed_sql PRIVATE
	${CMAKE_SOURCE_DIR}/mtpgsql/src/include/interface
)

if (APPLE)
	set(_embed_rpath "@loader_path/../lib")
else()
	set(_embed_rpath "\$ORIGIN/../lib")
endif()

set_target_properties(weaver_embed_sql PROPERTIES
	BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
	INSTALL_RPATH "${_embed_rpath}"
	BUILD_WITH_INSTALL_RPATH FALSE
)
