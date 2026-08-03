# Vacuum / index crash consistency

## Invariant

Lazy VACUUM deletes index TIDs first, then durably flushes (`FlushAllDirtyBuffersDurable`), then clears/recycles heap line pointers. After a crash past the barrier, the next vacuum can finish heap cleanup safely. Reversing that order can leave index TIDs pointing at recycled heap slots.

## Implementation notes

- Barrier: [`vacuumlazy.c`](../mtpgsql/src/backend/env/vacuumlazy.c) `lazy_index_barrier_then_heap`
- Durable flush: [`dbwriter.c`](../mtpgsql/src/backend/env/dbwriter.c) `FlushAllDirtyBuffersDurable` → `CommitPackage` / `smgrsync`
- Unused heap LPs are recorded so orphan index TIDs can be bulk-deleted after a crash
- Index under-delete vs heap dead count logs an incomplete-vacuum resume and continues heap cleanup; unused-LP orphan backstop may `AddRecoverRequest`
- Recoverpage backstops (amfreetuple slot): btree `btrecoverpage`, IVFFlat `ivfflatrecoverpage`, HNSW `hnswrecoverpage` — drop index entries / heaptids that point at unused heap LPs after crash recovery

## Crash injection

| Value (`WEAVER_VACUUM_CRASH_POINT` or property `vacuum_crash_point`) | Behavior |
|---|---|
| `after_index_barrier` | FATAL after durable index flush, before heap cleanup (safe restart) |
| `skip_barrier_heap_then_crash` | Skip durable flush, clean heap, FATAL (orphan index; recover on next vacuum) |

## Tests

```bash
# JUnit (named VACUUM path)
./gradlew :pgjava_c:test --tests 'org.weaverdb.VacuumIndexCrashConsistencyTest'

# CLI crash-ordering smoke (requires build/mtpg)
chmod +x mtpgsql/scripts/vacuum_crash_ordering_smoke.sh
./mtpgsql/scripts/vacuum_crash_ordering_smoke.sh
```
