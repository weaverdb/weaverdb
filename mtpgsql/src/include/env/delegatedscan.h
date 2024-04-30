/*-------------------------------------------------------------------------
 *
 * nodeDelegatedSeqscan.h
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef DELEGATEDSCAN_H
#define DELEGATEDSCAN_H


#include "utils/tqual.h"
#include "storage/itemptr.h"
#include "executor/tuptable.h"

typedef struct DelegateData* Delegate;
typedef struct MarkerData* Marker;

PG_EXTERN Marker DelegatedScanStart(void*(*scan_method)(Delegate), void* scan_args);
PG_EXTERN bool DelegatedScanNext(Marker pointer, ItemPointer  ret_item);
PG_EXTERN void DelegatedScanEnd(Marker pointer);

PG_EXTERN bool DelegatedGetTuple(Marker marker, Relation rel, Snapshot time, TupleTableSlot* slot, ItemPointer pointer, Buffer * buffer);

PG_EXTERN void* DelegatedScanArgs(Delegate delegate);
PG_EXTERN bool DelegatedTransferPointers(Delegate delegate,ItemPointer items,int size);
PG_EXTERN bool DelegatedCollectorWaiting(Delegate delegate);
PG_EXTERN void DelegatedDone(Delegate delegate);

PG_EXTERN int DelegatedGetTransferMax(void);
PG_EXTERN void DelegatedSetTransferMax(int max);


#endif	 /* NODESEQSCAN_H */
