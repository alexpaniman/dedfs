// The hash functions differ in both their internal mechanisms and in their outputs.

#ifndef _MURMURHASH3_H_
#define _MURMURHASH3_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//-----------------------------------------------------------------------------

// Has the lowest throughput, but also the lowest latency. If you're
// making a hash table that usually has small keys, this is probably
// the one you want to use on 32-bit machines. It has a 32-bit output.
void murmur3_x86_32 (const void *key, int len, uint32_t seed, void *out);

// Is also designed for 32-bit systems, but produces a 128-bit output,
// and has about 30% higher throughput than the previous hash. Be
// warned, though, that its latency for a single 16-byte key is about 86% longer!
void murmur3_x86_128(const void *key, int len, uint32_t seed, void *out);
     
// Is the best of the lot, if you're using a 64-bit machine. Its
// throughput is 250% higher than MurmurHash3_x86_32, but it has
// roughly the same latency. It has a 128-bit output.
void murmur3_x64_128(const void *key, int len, uint32_t seed, void *out);

//-----------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif

#endif // _MURMURHASH3_H_
