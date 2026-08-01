/*-------------------------------------------------------------------------
 *
 * snapmgr.h
 *	  Snapshot manager names expected by modern PG sources (pgvector, etc.)
 *
 * Weaver's MVCC snapshots live in utils/tqual.h / tqual.c (SnapshotHolder).
 * There is no separate snapmgr.c; map the modern entry points onto that.
 *
 *-------------------------------------------------------------------------
 */
#ifndef UTILS_SNAPMGR_H
#define UTILS_SNAPMGR_H

#include "utils/tqual.h"

/*
 * GetTransactionSnapshot — obtain the current transaction's query snapshot.
 *
 * Equivalent to modern PostgreSQL GetTransactionSnapshot(): ensure a snapshot
 * is installed for this xact/query, then return it. Weaver does that via
 * SetQuerySnapshot() (isolation-aware) and SnapshotHolder->QuerySnapshot.
 *
 * Do not return a null stub: HeapTupleSatisfiesVisibility treats SnapshotNow
 * as ((Snapshot) 0), so a null "snapshot" is silently "now" visibility.
 */
static inline Snapshot
GetTransactionSnapshot(void)
{
	SetQuerySnapshot();
	return GetSnapshotQuery(GetSnapshotHolder());
}

/*
 * RegisterSnapshot / UnregisterSnapshot — modern refcounted snapshot pins.
 * Weaver's QuerySnapshot is owned by SnapshotHolder for the xact lifetime;
 * concurrent-index parallel paths that call these are currently #if 0.
 */
static inline Snapshot
RegisterSnapshot(Snapshot snapshot)
{
	return snapshot;
}

static inline void
UnregisterSnapshot(Snapshot snapshot)
{
	(void) snapshot;
}

#endif /* UTILS_SNAPMGR_H */
