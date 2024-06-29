
find_package(JNI REQUIRED)

# create the executable
add_library(adt OBJECT bool.c cash.c char.c 
	date.c datetime.c datum.c filename.c float.c 
	int.c int8.c like.c lztext.c 
	misc.c nabstime.c name.c not_in.c numeric.c numutils.c 
	oid.c  
	regexp.c regproc.c 
    selfuncs.c sets.c 
	tid.c timestamp.c varbit.c varchar.c varlena.c version.c 
	network.c mac.c inet_net_ntop.c inet_net_pton.c 
	pg_lzcompress.c pg_locale.c formatting.c 
    ruleutils.c ri_triggers.c java.c wrapdatum.c md5.c sha2.c
)

add_dependencies(adt JNI::JNI fmgrtab)

if (JNI_FOUND)
    message ("JNI found")
    target_include_directories(adt PRIVATE ${JNI_INCLUDE_DIRS})
endif()