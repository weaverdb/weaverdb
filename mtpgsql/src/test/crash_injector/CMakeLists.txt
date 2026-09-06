# Test-only shared object. Never linked into libweaver, libweaver_jni, or postgres.
# Loaded at runtime via DYLD_INSERT_LIBRARIES / LD_PRELOAD by the crash harness.

add_library(weaver_crash_injector SHARED crash_injector.c)

set_target_properties(weaver_crash_injector PROPERTIES
	OUTPUT_NAME "weaver_crash_injector"
	PREFIX "lib"
)

if(CMAKE_DL_LIBS)
	target_link_libraries(weaver_crash_injector PRIVATE ${CMAKE_DL_LIBS})
endif()

if(APPLE)
	# Allow insertion into the locally-built unsigned postgres binary.
	target_link_options(weaver_crash_injector PRIVATE
		"-Wl,-undefined,dynamic_lookup")
endif()
