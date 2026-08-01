/* Snapshot manager shims for callers expecting modern utils/snapmgr.h.
 * Full snapmgr is not present; GetTransactionSnapshot is unused on the
 * Weaver pthread parallel-build path (upstream DSM block is #if 0).
 */
#ifndef UTILS_SNAPMGR_H
#define UTILS_SNAPMGR_H

#include "utils/tqual.h"

#ifndef GetTransactionSnapshot
#define GetTransactionSnapshot() ((Snapshot) 0)
#endif

#endif /* UTILS_SNAPMGR_H */
