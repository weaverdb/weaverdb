/* Stub condition_variable.h for pgvector */

#ifndef STORAGE_CONDITION_VARIABLE_H
#define STORAGE_CONDITION_VARIABLE_H

struct ConditionVariable
{
	/* minimal; pgvector hnsw uses it for some parallel/leader sync but we stub for build */
	int			dummy;
};
typedef struct ConditionVariable ConditionVariable;

#endif /* STORAGE_CONDITION_VARIABLE_H */
