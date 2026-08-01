/* Minimal bitutils stub for pgvector on WeaverDB (old PG) */

#ifndef PORT_PG_BITUTILS_H
#define PORT_PG_BITUTILS_H

#include <stdint.h>

/* popcount table from Postgres */
static const uint8_t pg_number_of_ones[256] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8
};

static inline uint64_t
pg_popcount64(uint64_t x)
{
#ifdef __GNUC__
    return __builtin_popcountll(x);
#else
    uint64_t count = 0;
    while (x) {
        count += x & 1;
        x >>= 1;
    }
    return count;
#endif
}

static inline int
pg_popcount(const char *buf, int bytes)
{
    int count = 0;
    const uint64_t *p = (const uint64_t *) buf;
    int nwords = bytes / 8;
    for (int i = 0; i < nwords; i++) {
        count += pg_popcount64(p[i]);
    }
    for (int i = nwords * 8; i < bytes; i++) {
        uint8_t b = (uint8_t)buf[i];
        count += pg_number_of_ones[b];
    }
    return count;
}

static inline uint64
pg_nextpower2_64(uint64 size)
{
	if (size == 0)
		return 1;
	size--;
	size |= size >> 1;
	size |= size >> 2;
	size |= size >> 4;
	size |= size >> 8;
	size |= size >> 16;
	size |= size >> 32;
	return size + 1;
}

#endif /* PORT_PG_BITUTILS_H */