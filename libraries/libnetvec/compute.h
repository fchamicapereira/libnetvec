#pragma once

#include <libutil/types.h>

#include <stdint.h>
#include <stdbool.h>
#include <immintrin.h>

inline u64 ensure_power_of_two(u64 n) {
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;
  n++;
  return n;
}

inline bool is_power_of_two(u64 n) { return (n & (n - 1)) == 0; }

inline bool is_prime(u64 n) {
  if (n <= 1)
    return false;
  if (n <= 3)
    return true;

  // Slow but simple
  for (u64 i = 2; i < n; i++) {
    if (n % i == 0) {
      return false;
    }
  }

  return true;
}

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
  u32 hash                 = 0;
  hash                     = (hash ^ *(u32 *)key) * magic_constant;
  hash                     = (hash ^ *((u32 *)key + 1)) * magic_constant;
  hash                     = (hash ^ *((u32 *)key + 2)) * magic_constant;
  hash                     = (hash ^ *((u32 *)key + 3)) * magic_constant;
  return hash;
}