#pragma once

#include <libutil/types.h>
#include <libutil/zmm.h>

#include <immintrin.h>
#include <assert.h>

template <size_t N> inline u32 crc32hash(void *key) {
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

template <> inline u32 crc32hash<16>(void *key) {
  u32 hash = 0;
  hash     = __builtin_ia32_crc32si(hash, *(u32 *)key);
  hash     = __builtin_ia32_crc32si(hash, *((u32 *)key + 1));
  hash     = __builtin_ia32_crc32si(hash, *((u32 *)key + 2));
  hash     = __builtin_ia32_crc32si(hash, *((u32 *)key + 3));
  return hash;
}

template <size_t N> inline u32 fxhash(void *key) {
  const u64 magic_constant = 0x517cc1b727220a95ULL;
  size_t key_size          = N;
  u32 hash                 = 0;
  while (key_size > 0) {
    if (key_size >= sizeof(u32)) {
      hash = (hash ^ *(u32 *)key) * magic_constant;
      key  = (u32 *)key + 1;
      key_size -= sizeof(u32);
    } else {
      u32 c = *(u8 *)key;
      hash  = (hash ^ c) * magic_constant;
      key   = (u8 *)key + 1;
      key_size -= 1;
    }
  }
  return hash;
}

template <> inline u32 fxhash<16>(void *key) {
  const u64 magic_constant = 0x517cc1b727220a95ULL;

  u64 hash = 0;

  hash = (hash ^ *(u64 *)key) * magic_constant;
  hash = (hash ^ *((u64 *)key + 1)) * magic_constant;

  hash ^= hash >> 32; // Final avalanche mix

  return hash;
}

template <size_t key_size> inline __m256i fxhash_vec8(void *keys) {
  assert(false && "fxhash_vec8 is not implemented yet");
  printf("ERROR: fxhash_vec8 is not implemented yet\n");
  exit(1);
}

template <> inline __m256i fxhash_vec8<16>(void *keys) {
  const __m512i magic_constant = _mm512_set1_epi64(0x517cc1b727220a95ULL);
  __m512i hash                 = _mm512_setzero_si512();

  // Grab 4B at a time from each key and process them in parallel
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

  // Convert from __m512i to __m256i
  return _mm512_extracti64x4_epi64(hash, 0);
}