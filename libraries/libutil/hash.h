#pragma once

#include <libutil/types.h>
#include <libutil/zmm.h>

#include <immintrin.h>
#include <assert.h>

template <size_t N> inline u32 crc32hash(const void *key) {
  size_t key_size = N;
  u32 hash        = 0;
  while (key_size > 0) {
    if (key_size >= sizeof(u32)) {
      hash = __builtin_ia32_crc32si(hash, *(u32 *)key);
      key  = (u32 *)key + 1;
      key_size -= sizeof(u32);
    } else {
      u32 c = *(u8 *)key;
      hash  = __builtin_ia32_crc32si(hash, c);
      key   = (u8 *)key + 1;
      key_size -= 1;
    }
  }
  return hash;
}

template <> inline u32 crc32hash<16>(const void *key) {
  u32 hash = 0;
  hash     = __builtin_ia32_crc32si(hash, *(u32 *)key);
  hash     = __builtin_ia32_crc32si(hash, *((u32 *)key + 1));
  hash     = __builtin_ia32_crc32si(hash, *((u32 *)key + 2));
  hash     = __builtin_ia32_crc32si(hash, *((u32 *)key + 3));
  return hash;
}

template <size_t N> inline u32 fxhash(const void *key) {
  assert(false && "fxhash is not implemented yet");
  printf("ERROR: fxhash is not implemented yet\n");
  exit(1);
}

template <> inline u32 fxhash<16>(const void *key) {
  const u64 magic_constant = 0x517cc1b727220a95ULL;

  u64 hash = 0;

  hash = (hash ^ *(u64 *)key) * magic_constant;
  hash = (hash ^ *((u64 *)key + 1)) * magic_constant;

  hash ^= hash >> 32; // Final avalanche mix

  return hash;
}

template <size_t key_size> inline __m256i fxhash_vec8(const void *keys) {
  assert(false && "fxhash_vec8 is not implemented yet");
  printf("ERROR: fxhash_vec8 is not implemented yet\n");
  exit(1);
}

template <> inline __m256i fxhash_vec8<16>(const void *keys) {
  const __m512i magic_constant = _mm512_set1_epi64(0x517cc1b727220a95ULL);

  __m512i hash = _mm512_setzero_si512();

  __m512i base_offsets = _mm512_set_epi64(7 * 16, 6 * 16, 5 * 16, 4 * 16, 3 * 16, 2 * 16, 1 * 16, 0 * 16);
  __m512i keysp_vec    = _mm512_add_epi64(_mm512_set1_epi64((u64)keys), base_offsets);

  __m512i keys_vec;

  // First 8B
  keys_vec = _mm512_i64gather_epi64(keysp_vec, NULL, 1);
  hash     = _mm512_xor_si512(hash, keys_vec);
  hash     = _mm512_mullo_epi64(hash, magic_constant);

  keysp_vec = _mm512_add_epi64(keysp_vec, _mm512_set1_epi64(8));

  // Second 8B
  keys_vec = _mm512_i64gather_epi64(keysp_vec, NULL, 1);
  hash     = _mm512_xor_si512(hash, keys_vec);
  hash     = _mm512_mullo_epi64(hash, magic_constant);

  // Final avalanche mix
  hash = _mm512_xor_si512(hash, _mm512_srli_epi64(hash, 32));

  // Convert from __m512i to __m256i, keeping the lower 32b of each hash
  __m256i hash_256 = _mm512_cvtepi64_epi32(hash);

  return hash_256;
}

template <size_t key_size> inline __m512i fxhash_vec16(const void *keys) {
  assert(false && "fxhash_vec16 is not implemented yet");
  printf("ERROR: fxhash_vec16 is not implemented yet\n");
  exit(1);
}

template <> inline __m512i fxhash_vec16<16>(const void *keys) {
  const __m512i magic_constant = _mm512_set1_epi64(0x517cc1b727220a95ULL);

  __m512i hash_lo = _mm512_setzero_si512();
  __m512i hash_hi = _mm512_setzero_si512();

  __m512i base_lo_offsets = _mm512_set_epi64(7 * 16, 6 * 16, 5 * 16, 4 * 16, 3 * 16, 2 * 16, 1 * 16, 0 * 16);
  __m512i keysp_lo_vec    = _mm512_add_epi64(_mm512_set1_epi64((u64)keys), base_lo_offsets);

  __m512i base_hi_offsets = _mm512_set_epi64(15 * 16, 14 * 16, 13 * 16, 12 * 16, 11 * 16, 10 * 16, 9 * 16, 8 * 16);
  __m512i keysp_hi_vec    = _mm512_add_epi64(_mm512_set1_epi64((u64)keys), base_hi_offsets);

  __m512i keys_lo_vec;
  __m512i keys_hi_vec;

  // First 8B
  keys_lo_vec = _mm512_i64gather_epi64(keysp_lo_vec, NULL, 1);
  hash_lo     = _mm512_xor_si512(hash_lo, keys_lo_vec);
  hash_lo     = _mm512_mullo_epi64(hash_lo, magic_constant);

  keys_hi_vec = _mm512_i64gather_epi64(keysp_hi_vec, NULL, 1);
  hash_hi     = _mm512_xor_si512(hash_hi, keys_hi_vec);
  hash_hi     = _mm512_mullo_epi64(hash_hi, magic_constant);

  keysp_lo_vec = _mm512_add_epi64(keysp_lo_vec, _mm512_set1_epi64(8));
  keysp_hi_vec = _mm512_add_epi64(keysp_hi_vec, _mm512_set1_epi64(8));

  // Second 8B
  keys_lo_vec = _mm512_i64gather_epi64(keysp_lo_vec, NULL, 1);
  hash_lo     = _mm512_xor_si512(hash_lo, keys_lo_vec);
  hash_lo     = _mm512_mullo_epi64(hash_lo, magic_constant);

  keys_hi_vec = _mm512_i64gather_epi64(keysp_hi_vec, NULL, 1);
  hash_hi     = _mm512_xor_si512(hash_hi, keys_hi_vec);
  hash_hi     = _mm512_mullo_epi64(hash_hi, magic_constant);

  // Final avalanche mix
  hash_lo = _mm512_xor_si512(hash_lo, _mm512_srli_epi64(hash_lo, 32));
  hash_hi = _mm512_xor_si512(hash_hi, _mm512_srli_epi64(hash_hi, 32));

  // Convert from __m512i to __m256i
  __m256i hash_lo_256 = _mm512_cvtepi64_epi32(hash_lo);
  __m256i hash_hi_256 = _mm512_cvtepi64_epi32(hash_hi);

  // Merge the two 256-bit halves into a single 512-bit vector
  __m512i hash = _mm512_castsi256_si512(hash_lo_256);
  hash         = _mm512_inserti32x8(hash, hash_hi_256, 1);

  return hash;
}

template <size_t N> inline u32 djb2hash(const void *key) {
  size_t key_size = N;
  u32 hash        = 5381;
  for (size_t i = 0; i < key_size; i++) {
    u8 c = ((u8 *)key)[i];
    hash = ((hash << 5) + hash) + c; // hash * 33 + c
  }
  return hash;
}

template <> inline u32 djb2hash<16>(const void *key) {
  u32 hash = 5381;
  hash     = ((hash << 5) + hash) + ((u8 *)key)[0];
  hash     = ((hash << 5) + hash) + ((u8 *)key)[1];
  hash     = ((hash << 5) + hash) + ((u8 *)key)[2];
  hash     = ((hash << 5) + hash) + ((u8 *)key)[3];
  hash     = ((hash << 5) + hash) + ((u8 *)key)[4];
  hash     = ((hash << 5) + hash) + ((u8 *)key)[5];
  hash     = ((hash << 5) + hash) + ((u8 *)key)[6];
  hash     = ((hash << 5) + hash) + ((u8 *)key)[7];
  hash     = ((hash << 5) + hash) + ((u8 *)key)[8];
  hash     = ((hash << 5) + hash) + ((u8 *)key)[9];
  hash     = ((hash << 5) + hash) + ((u8 *)key)[10];
  hash     = ((hash << 5) + hash) + ((u8 *)key)[11];
  hash     = ((hash << 5) + hash) + ((u8 *)key)[12];
  hash     = ((hash << 5) + hash) + ((u8 *)key)[13];
  hash     = ((hash << 5) + hash) + ((u8 *)key)[14];
  hash     = ((hash << 5) + hash) + ((u8 *)key)[15];
  return hash;
}

template <size_t N> inline u32 murmur3hash(const void *key) {
  size_t key_size = N;

  const u32 c1 = 0xcc9e2d51;
  const u32 c2 = 0x1b873593;
  const u32 r1 = 15;
  const u32 r2 = 13;
  const u32 m  = 5;
  const u32 n  = 0xe6546b64;

  u32 hash = 0;

  // Process 4-byte chunks
  const int nblocks = key_size / 4;
  const u32 *blocks = (const u32 *)key;

  for (int i = 0; i < nblocks; i++) {
    u32 k = blocks[i];

    k *= c1;
    k = _rotl(k, r1);
    k *= c2;

    hash ^= k;
    hash = _rotl(hash, r2);
    hash = hash * m + n;
  }

  // Handle remaining bytes
  const u8 *tail = (const u8 *)(key) + nblocks * 4;
  u32 k1         = 0;

  switch (key_size & 3) {
  case 3:
    k1 ^= tail[2] << 16;
  case 2:
    k1 ^= tail[1] << 8;
  case 1:
    k1 ^= tail[0];
    k1 *= c1;
    k1 = _rotl(k1, r1);
    k1 *= c2;
    hash ^= k1;
  }

  // Finalization
  hash ^= key_size;
  hash ^= hash >> 16;
  hash *= 0x85ebca6b;
  hash ^= hash >> 13;
  hash *= 0xc2b2ae35;
  hash ^= hash >> 16;

  return hash;
}