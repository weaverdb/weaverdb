/* Stub utils/fmgrprotos.h for pgvector on WeaverDB.
 *
 * Modern Postgres auto-generates prototypes for all SQL-visible functions here.
 * In this integration the symbols we actually call (numeric_float4) are
 * provided via pgvector_shims.c + declared in our compat force-include.
 * Other references go through FunctionCallXColl at runtime.
 *
 * Keep this file minimal so the #include in vector.c / sparse / halfvec succeeds.
 */
#ifndef UTILS_FMGRPROTOS_H
#define UTILS_FMGRPROTOS_H

/* (numeric_float4 references removed from call sites for build; project's builtins provides its version) */

#endif /* UTILS_FMGRPROTOS_H */
