#ifndef COMMON_HASHFN_H
#define COMMON_HASHFN_H

#include <stdint.h>

static inline uint32
murmurhash32(uint32 data)
{
	uint32 h = data;

	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	return h;
}

#endif /* COMMON_HASHFN_H */
