/* Minimal atomics stub for pgvector on WeaverDB (old PG) */

#ifndef PORT_ATOMICS_H
#define PORT_ATOMICS_H

#include <stdint.h>

#define pg_atomic_uint32 uint32_t
#define pg_atomic_uint64 uint64_t

static inline void
pg_atomic_init_u32(volatile pg_atomic_uint32 *ptr, uint32_t val)
{
    *ptr = val;
}

static inline uint32_t
pg_atomic_read_u32(volatile pg_atomic_uint32 *ptr)
{
    return *ptr;
}

static inline void
pg_atomic_write_u32(volatile pg_atomic_uint32 *ptr, uint32_t val)
{
    *ptr = val;
}

static inline uint32_t
pg_atomic_fetch_add_u32(volatile pg_atomic_uint32 *ptr, uint32_t inc)
{
#ifdef __GNUC__
    return __sync_fetch_and_add(ptr, inc);
#else
    uint32_t old = *ptr;
    *ptr += inc;
    return old;
#endif
}

#define pg_memory_barrier()		__sync_synchronize()

#endif /* PORT_ATOMICS_H */