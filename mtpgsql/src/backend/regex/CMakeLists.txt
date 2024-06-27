

add_library(regex OBJECT regcomp.c regerror.c regexec.c regfree.c)

target_compile_options(regex PRIVATE -Wno-tautological-constant-out-of-range-compare)