/* Stub condition_variable.h for pgvector */

#ifndef STORAGE_CONDITION_VARIABLE_H
#define STORAGE_CONDITION_VARIABLE_H

struct ConditionVariable
{
	int			dummy;
};
typedef struct ConditionVariable ConditionVariable;

static inline void
ConditionVariableInit(ConditionVariable *cv)
{
	(void) cv;
}

static inline void
ConditionVariableSleep(ConditionVariable *cv, int wait_event)
{
	(void) cv;
	(void) wait_event;
}

static inline void
ConditionVariableCancelSleep(void)
{
}

static inline void
ConditionVariableSignal(ConditionVariable *cv)
{
	(void) cv;
}

#endif /* STORAGE_CONDITION_VARIABLE_H */
