/* Stub utils/snapmgr.h for pgvector */
#ifndef UTILS_SNAPMGR_H
#define UTILS_SNAPMGR_H

#include "utils/tqual.h"

#ifndef GetTransactionSnapshot
#define GetTransactionSnapshot() ((Snapshot) 0)
#endif
#ifndef GetActiveSnapshot
#define GetActiveSnapshot() ((Snapshot) 0)
#endif

#endif /* UTILS_SNAPMGR_H */
