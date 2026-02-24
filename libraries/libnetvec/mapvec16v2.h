#pragma once

#include <libutil/hash.h>
#include <libutil/math.h>
#include <libutil/zmm.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <immintrin.h>
#include <assert.h>

#include <iostream>
#include <format>
#include <sstream>

template <size_t key_size> class MapVec16v2 {
public:
  static constexpr const u32 VECTOR_SIZE       = 16;
  static constexpr const u32 SPECIAL_NULL_HASH = 0;

private:
  const u32 capacity;

  void **keyps;
  u32 *khs;
  int *vals;

  u32 size;

public:
  MapVec16v2(u32 _capacity) : capacity(_capacity), size(0) {
    // Check that capacity is a power of 2
    if (_capacity == 0 || is_power_of_two(_capacity) == 0) {
      fprintf(stderr, "Error: Capacity must be a power of 2\n");
      exit(1);
    }

    keyps = (void **)malloc(sizeof(void *) * _capacity);
    khs   = (u32 *)malloc(sizeof(u32) * _capacity);
    vals  = (int *)malloc(sizeof(int) * _capacity);

    for (u32 i = 0; i < capacity; ++i) {
      khs[i] = SPECIAL_NULL_HASH;
    }
  }

  ~MapVec16v2() {
    free(keyps);
    free(khs);
    free(vals);
  }

  // FIXME: this should return an array of ints indicating successful reads.
  int get_vec(void *keys, int *values_out) const {
    // Create a mask with all bits set to 1.
    // This mask will be updated in each iteration of the loop, indicating the lanes that are still pending.
    __mmask16 mask = 0xffff;

    // Offset vector for linear probing, starting at 0.
    __m512i offset = _mm512_setzero_si512();

    // Load the hashes into a vector register
    __m512i hashes_vec = hash_keys_vec(keys);
    // printf("hashes_vec: %s\n", zmm512_32b_to_str(hashes_vec).c_str());

    u32 pending = VECTOR_SIZE;
    while (pending != 0) {
      // Add offset to hashes to get the current indices
      __m512i indices_vec = _mm512_add_epi32(hashes_vec, offset);

      // & capacity - 1 to get the indices within the capacity
      indices_vec = _mm512_and_epi32(indices_vec, _mm512_set1_epi32(capacity - 1));
      // printf("indices_vec: %s\n", zmm512_32b_to_str(indices_vec).c_str());

      // Selectively gather hashes using the mask
      __m512i khs_vec = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), mask, indices_vec, khs, sizeof(u32));

      // Create a mask for lanes where slots are not empty and hashes match
      __mmask16 not_empty_cmp = _mm512_cmpneq_epi32_mask(khs_vec, _mm512_set1_epi32(SPECIAL_NULL_HASH));
      __mmask16 hash_cmp      = _mm512_cmpeq_epi32_mask(khs_vec, hashes_vec);
      __mmask16 match_mask    = _mm512_kand(not_empty_cmp, hash_cmp);

      // When the slot is empty and the key is not found, we can stop probing for that lane.
      mask = _mm512_kand(not_empty_cmp, mask);

      // Load the keys into vector registers, first 8 pointers (0-7) into the 'low' register and next 8 pointers (8-15) into the 'high' register
      __m512i base_offsets        = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
      __m512i target_keysp_base   = _mm512_set1_epi64((u64)keys);
      __m512i target_keysp_lo_vec = _mm512_add_epi64(target_keysp_base, _mm512_mullo_epi64(base_offsets, _mm512_set1_epi64(key_size)));
      target_keysp_base           = _mm512_set1_epi64((u64)keys + 8 * key_size);
      __m512i target_keysp_hi_vec = _mm512_add_epi64(target_keysp_base, _mm512_mullo_epi64(base_offsets, _mm512_set1_epi64(key_size)));

      // Print keys_lo_vec and keys_hi_vec for debugging
      // printf("keys_lo_vec: %s\n", zmm512_64b_to_str(keys_lo_vec).c_str());
      // printf("keys_hi_vec: %s\n", zmm512_64b_to_str(keys_hi_vec).c_str());
      // printf("indices_vec: %s\n", zmm512_32b_to_str(indices_vec).c_str());

      __m256i indices_lo = _mm512_castsi512_si256(indices_vec);
      __m256i indices_hi = _mm512_extracti32x8_epi32(indices_vec, 1);

      // Load the keys from memory for the lanes where the match_mask is set
      // These 512b registers contain 64b pointers, so we need to gather them in two parts (lo and hi) and then combine them for comparison.
      // So keysp_lo_vec has 8 pointers (0-7) and keyps_hi_vec has the next 8 pointers (8-15).
      __m512i keysp_lo_vec = _mm512_mask_i32gather_epi64(_mm512_setzero_si512(), match_mask, indices_lo, keyps, sizeof(void *));
      __m512i keyps_hi_vec = _mm512_mask_i32gather_epi64(_mm512_setzero_si512(), match_mask >> 8, indices_hi, keyps, sizeof(void *));

      for (u32 bytes_compared = 0; bytes_compared < key_size; bytes_compared += 4) {
        if (key_size - bytes_compared >= 4) {
          // Compare the gathered keys with the input keys to confirm matches
          // Keys can be arbitrarily large, so we need to compare them 32b at a time.
          // Gather the next 32b of the keys for comparison
          __mmask8 lo_mask           = _mm512_kand(match_mask, 0x00ff); // Mask for the lower 8 lanes
          __m256i keys_lo_vec        = _mm512_mask_i64gather_epi32(_mm256_setzero_si256(), lo_mask, keysp_lo_vec, NULL, 1);
          __m256i target_keys_lo_vec = _mm512_mask_i64gather_epi32(_mm256_setzero_si256(), lo_mask, target_keysp_lo_vec, NULL, 1);
          __mmask8 lo_match          = _mm256_cmpeq_epi32_mask(keys_lo_vec, target_keys_lo_vec);

          // Compared 8 pointers in the 'lo' register, now we need to update the mask for those 8 bits
          __mmask8 hi_mask           = _mm512_kand(match_mask >> 8, 0x00ff); // Mask for the upper 8 lanes
          __m256i keys_hi_vec        = _mm512_mask_i64gather_epi32(_mm256_setzero_si256(), hi_mask, keyps_hi_vec, NULL, 1);
          __m256i target_keys_hi_vec = _mm512_mask_i64gather_epi32(_mm256_setzero_si256(), hi_mask, target_keysp_hi_vec, NULL, 1);
          __mmask8 hi_match          = _mm256_cmpeq_epi32_mask(keys_hi_vec, target_keys_hi_vec);

          // Not ideal, we are bouncing from GPR to ZMM, but I don't see any other alternative.
          match_mask = _mm512_kand(match_mask, ((__mmask16)hi_match << 8) | (__mmask16)lo_match);
        } else {
          // Handle the last few bytes that are less than 4
          // This is more complex because we can't directly compare with SIMD instructions.
          // We would need to create masks for the remaining bytes and compare them manually.
          // For simplicity, we'll skip this part in this implementation.
          assert(false && "TODO: Handle remaining bytes in get_vec");
        }

        // Advance key pointers by 4 bytes for the next iteration
        keysp_lo_vec        = _mm512_add_epi64(keysp_lo_vec, _mm512_set1_epi64(4));
        keyps_hi_vec        = _mm512_add_epi64(keyps_hi_vec, _mm512_set1_epi64(4));
        target_keysp_lo_vec = _mm512_add_epi64(target_keysp_lo_vec, _mm512_set1_epi64(4));
        target_keysp_hi_vec = _mm512_add_epi64(target_keysp_hi_vec, _mm512_set1_epi64(4));
      }

      // Gather the values for the lanes where the mask is set
      __m512i values_vec = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), match_mask, indices_vec, vals, sizeof(int));

      // Load the values from the vector register to the output array for the lanes where the mask is set
      _mm512_mask_storeu_epi32((void *)values_out, match_mask, values_vec);

      // Update pending count and loop offset for the next iteration
      mask = _mm512_kandn(match_mask, mask);

      // Increment the offset only for the pending keys
      offset = _mm512_mask_add_epi32(offset, mask, offset, _mm512_set1_epi32(1));

      // If offset == capacity, set the mask to 0 to prevent further probing
      if (_mm512_mask_cmpeq_epi32_mask(mask, offset, _mm512_set1_epi32(capacity))) {
        mask = 0;
      }

      // Count the number of bits set to 1 in the mask
      pending = _cvtmask16_u32(_mm_popcnt_u32(mask));
    }

    // TODO: Track the successful reads.
    return 1;
  }

  void put_vec(void *keys, int *values) {
    assert(size + VECTOR_SIZE <= capacity);

    // Create a mask with all bits set to 1.
    // This mask will be updated in each iteration of the loop, indicating the lanes that are still pending.
    __mmask16 mask = 0xffff;

    // Offset vector for linear probing, starting at 0.
    __m512i offset = _mm512_setzero_si512();

    // Load the hashes into a vector register
    __m512i hashes_vec = hash_keys_vec(keys);
    // printf("hashes_vec: %s\n", zmm512_32b_to_str(hashes_vec).c_str());

    u32 pending = VECTOR_SIZE;
    while (pending != 0) {
      // Add offset to hashes to get the current indices
      __m512i indices_vec = _mm512_add_epi32(hashes_vec, offset);

      // & capacity - 1 to get the indices within the capacity
      indices_vec = _mm512_and_epi32(indices_vec, _mm512_set1_epi32(capacity - 1));
      // printf("indices_vec: %s\n", zmm512_32b_to_str(indices_vec).c_str());

      // Detect conflicts between the active lanes.
      // This returns a vector where each lane contains a bitmask of previous lanes that have the same index.
      // This is a **heavy** operation, but the alternative would be a gather/scatter approach, equally expensive.
      __m512i conflicts = _mm512_mask_conflict_epi32(_mm512_setzero_si512(), mask, indices_vec);

      // A lane can only proceed if it has NO conflicts with previous lanes
      // _mm512_testn_epi32_mask returns true where (src1 & src2) == 0
      __mmask16 no_conflict_mask = _mm512_mask_testn_epi32_mask(mask, conflicts, _mm512_set1_epi32(0xffffffff));
      no_conflict_mask           = _mm512_kand(no_conflict_mask, mask);

      // Selectively gather hashes using the mask
      __m512i khs_vec = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), no_conflict_mask, indices_vec, khs, sizeof(u32));

      // Final mask of lanes that:
      // (A) Are still pending
      // (B) Don't conflict with a lane to their left
      // (C) Found an empty slot in memory
      __mmask16 insertion_mask = _mm512_mask_cmpeq_epi32_mask(no_conflict_mask, khs_vec, _mm512_set1_epi32(SPECIAL_NULL_HASH));

      // Store the hashes, keys, and values for the indices with empty slots
      _mm512_mask_i32scatter_epi32(khs, insertion_mask, indices_vec, hashes_vec, sizeof(u32));

      // Load the keys into vector registers, first 8 pointers (0-7) into the 'low' register and next 8 pointers (8-15) into the 'high' register
      __m512i base_offsets = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
      __m512i keys_base    = _mm512_set1_epi64((u64)keys);
      __m512i keys_lo_vec  = _mm512_add_epi64(keys_base, _mm512_mullo_epi64(base_offsets, _mm512_set1_epi64(key_size)));
      keys_base            = _mm512_set1_epi64((u64)keys + 8 * key_size);
      __m512i keys_hi_vec  = _mm512_add_epi64(keys_base, _mm512_mullo_epi64(base_offsets, _mm512_set1_epi64(key_size)));

      // Print keys_lo_vec and keys_hi_vec for debugging
      // printf("keys_lo_vec: %s\n", zmm512_64b_to_str(keys_lo_vec).c_str());
      // printf("keys_hi_vec: %s\n", zmm512_64b_to_str(keys_hi_vec).c_str());
      // printf("indices_vec: %s\n", zmm512_32b_to_str(indices_vec).c_str());

      // Gather the indices for the lo and hi keys for scattertering
      __m256i indices_lo = _mm512_castsi512_si256(indices_vec);
      __m256i indices_hi = _mm512_extracti32x8_epi32(indices_vec, 1);

      _mm512_mask_i32scatter_epi64(keyps, insertion_mask, indices_lo, keys_lo_vec, sizeof(void *));
      _mm512_mask_i32scatter_epi64(keyps, insertion_mask >> 8, indices_hi, keys_hi_vec, sizeof(void *));

      _mm512_mask_i32scatter_epi32(vals, insertion_mask, indices_vec, _mm512_loadu_si512((void *)values), sizeof(int));

      // Set the mask to 0 for indices with empty slots
      mask = _mm512_kandn(insertion_mask, mask);

      // Increment the offset only for the pending keys
      offset = _mm512_mask_add_epi32(offset, mask, offset, _mm512_set1_epi32(1));

      // If offset == capacity, set the mask to 0 to prevent further probing
      if (_mm512_mask_cmpeq_epi32_mask(mask, offset, _mm512_set1_epi32(capacity))) {
        mask = 0;
      }

      // Count the number of bits set to 1 in the mask
      pending = _cvtmask16_u32(_mm_popcnt_u32(mask));
    }

    size += VECTOR_SIZE;
  }

  void erase_vec(void *keys);

  int get(void *key, int *value_out) const {
    const u32 hash = crc32hash<key_size>(key);

    for (u32 i = 0; i < capacity; ++i) {
      const u32 index = loop(hash + i, capacity);
      const u32 kh    = khs[index];
      if (kh != SPECIAL_NULL_HASH && kh == hash) {
        if (keq(keyps[index], key)) {
          *value_out = vals[index];
          return 1;
        }
      }
    }

    return -1;
  }

  void put(void *key, int value) {
    const u32 hash = crc32hash<key_size>(key);

    for (u32 i = 0; i < capacity; ++i) {
      const u32 index = loop(hash + i, capacity);
      const u32 kh    = khs[index];
      if (kh == SPECIAL_NULL_HASH) {
        keyps[index] = key;
        khs[index]   = hash;
        vals[index]  = value;

        ++size;

        // printf("Put key %p with hash 0x%08x at index 0x%04x\n", key, hash, index);
        break;
      }
    }
  }

  void erase(void *key) {
    const u32 hash = crc32hash<key_size>(key);
    for (u32 i = 0; i < capacity; ++i) {
      const u32 index = loop(hash + i, capacity);
      const u32 kh    = khs[index];
      if (kh != SPECIAL_NULL_HASH && kh == hash) {
        if (keq(keyps[index], key)) {
          khs[index] = SPECIAL_NULL_HASH;
          --size;
          break;
        }
      }
    }
  }

  u32 get_size() const { return size; }

private:
  int keq(void *key1, void *key2) const { return memcmp(key1, key2, key_size) == 0; }

  u32 loop(u32 k, u32 capacity) const { return k & (capacity - 1); }

  __m512i hash_keys_vec(void *keys) const {
    // TODO: vectorize this
    // u32 hashes[VECTOR_SIZE];
    // for (u32 i = 0; i < VECTOR_SIZE; ++i) {
    //   void *key = (void *)((u8 *)keys + i * key_size);
    //   hashes[i] = crc32hash<key_size>(key);
    // }
    // assert(sizeof(hashes) == sizeof(__m512i));
    // __m512i hashes_vec = _mm512_loadu_si512((void *)hashes);
    // return hashes_vec;

    return fxhash_vec16_64b<key_size>(keys);
  }
};
