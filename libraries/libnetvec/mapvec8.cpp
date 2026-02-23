#include "mapvec8.h"
#include "compute.h"
#include "zmm.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <immintrin.h>
#include <assert.h>

#include <iostream>
#include <format>
#include <sstream>

namespace {

inline int keq(void *key1, void *key2, unsigned keys_size) { return memcmp(key1, key2, keys_size) == 0; }

inline unsigned loop(unsigned k, unsigned capacity) { return k & (capacity - 1); }

inline unsigned khash(void *key, unsigned key_size) {
  unsigned hash = 0;
  while (key_size > 0) {
    if (key_size >= sizeof(unsigned int)) {
      hash = __builtin_ia32_crc32si(hash, *(unsigned int *)key);
      key  = (unsigned int *)key + 1;
      key_size -= sizeof(unsigned int);
    } else {
      unsigned int c = *(unsigned char *)key;
      hash           = __builtin_ia32_crc32si(hash, c);
      key            = (unsigned char *)key + 1;
      key_size -= 1;
    }
  }
  return hash;
}

inline __m512i hash_keys_vec(void *keys, unsigned key_size) {
  // TODO: vectorize this
  uint64_t hashes[MapVec8::VECTOR_SIZE];
  for (unsigned i = 0; i < MapVec8::VECTOR_SIZE; ++i) {
    void *key = (void *)((unsigned char *)keys + i * key_size);
    hashes[i] = khash(key, key_size);
  }
  assert(sizeof(hashes) == sizeof(__m512i));
  __m512i hashes_vec = _mm512_loadu_si512((void *)hashes);
  return hashes_vec;
}

} // namespace

MapVec8::MapVec8(unsigned _capacity, unsigned _key_size) : capacity(_capacity), key_size(_key_size), size(0) {
  // Check that capacity is a power of 2
  if (_capacity == 0 || is_power_of_two(_capacity) == 0) {
    fprintf(stderr, "Error: Capacity must be a power of 2\n");
    exit(1);
  }

  hashes_values = (hash_value_t *)malloc(sizeof(hash_value_t) * _capacity);
  keyps         = (void **)malloc(sizeof(void *) * _capacity);

  for (unsigned i = 0; i < capacity; ++i) {
    hashes_values[i].hash  = SPECIAL_NULL_HASH;
    hashes_values[i].value = 0;
    keyps[i]               = nullptr;
  }
}

MapVec8::~MapVec8() {
  free(hashes_values);
  free(keyps);
}

void MapVec8::put_vec(void *keys, int *values) {
  assert(size + MapVec8::VECTOR_SIZE <= capacity);

  // Create a mask with all bits set to 1.
  // This mask will be updated in each iteration of the loop, indicating the lanes that are still pending.
  __mmask8 mask = 0xff;

  // Offset vector for linear probing, starting at 0.
  __m512i offset = _mm512_setzero_si512();

  // Load the hashes into a vector register
  __m512i hashes_vec = hash_keys_vec(keys, key_size);
  // printf("hashes_vec: %s\n", zmm512_64b_to_str(hashes_vec).c_str());

  unsigned pending = MapVec8::VECTOR_SIZE;
  while (pending != 0) {
    // Add offset to hashes to get the current indices
    __m512i indices_vec = _mm512_add_epi64(hashes_vec, offset);

    // & capacity - 1 to get the indices within the capacity
    indices_vec = _mm512_mask_and_epi64(_mm512_set1_epi64(0xffffffffffffffff), mask, indices_vec, _mm512_set1_epi64(capacity - 1));
    // printf("indices_vec:      %s\n", zmm512_64b_to_str(indices_vec).c_str());

    // Detect conflicts between the active lanes.
    // This returns a vector where each lane contains a bitmask of previous lanes that have the same index.
    // This is a **heavy** operation, but the alternative would be a gather/scatter approach, equally expensive.
    __m512i conflicts = _mm512_mask_conflict_epi64(_mm512_setzero_si512(), mask, indices_vec);
    // printf("conflicts:        %s\n", zmm512_64b_to_str(conflicts).c_str());

    // A lane can only proceed if it has NO conflicts with previous lanes
    // _mm512_testn_epi64_mask returns true where (src1 & src2) == 0
    __mmask8 no_conflict_mask = _mm512_mask_testn_epi64_mask(mask, conflicts, _mm512_set1_epi64(0xffffffffffffffff));
    no_conflict_mask          = _mm512_kand(no_conflict_mask, mask);

    // Selectively gather hashes and values with the mask
    __m512i map_hashes_values_vec = _mm512_mask_i64gather_epi64(_mm512_setzero_si512(), no_conflict_mask, indices_vec, hashes_values, sizeof(hash_value_t));

    // Mask the values out, get the hashes
    __m512i map_hashes_vec = _mm512_and_epi64(map_hashes_values_vec, _mm512_set1_epi64(0x00000000ffffffff));
    // printf("map_hashes_vec:   %s\n", zmm512_64b_to_str(map_hashes_vec).c_str());

    // Final mask of lanes that:
    // (A) Are still pending
    // (B) Don't conflict with a lane to their left
    // (C) Found an empty slot in memory
    __mmask8 insertion_mask = _mm512_mask_cmpeq_epi64_mask(no_conflict_mask, map_hashes_vec, _mm512_set1_epi64(SPECIAL_NULL_HASH));
    // printf("mask:             0b%s\n", std::format("{:08b}", mask).c_str());
    // printf("no_conflict_mask: 0b%s\n", std::format("{:08b}", no_conflict_mask).c_str());
    // printf("insertion_mask:   0b%s\n", std::format("{:08b}", insertion_mask).c_str());

    // Set hash and value for the indices where we will insert
    // Load the 8 values into the ZMM register
    __m256i values_256vec = _mm256_loadu_si256((__m256i *)values);

    // Expand these 8 values into the 16 slots of a 512-bit register.
    // We use a permutation to put them into the 'odd' dword slots.
    __m512i values_vec = _mm512_castsi256_si512(values_256vec);
    values_vec         = _mm512_permutexvar_epi32(_mm512_set_epi32(7, 16, 6, 16, 5, 16, 4, 16, 3, 16, 2, 16, 1, 16, 0, 16), values_vec);
    // printf("values_vec:       %s\n", zmm512_64b_to_str(values_vec).c_str());

    // Blend values and hashes.
    // Mask 0xAAAA (10101010...) selects the odd dwords (values) and keeps even dwords (hashes).
    __m512i combined = _mm512_mask_blend_epi32(0xAAAA, hashes_vec, values_vec);
    // printf("combined:         %s\n", zmm512_64b_to_str(combined).c_str());

    // Scatter the combined hash+value structs into the map for the lanes where insertion_mask is set
    _mm512_mask_i64scatter_epi64(hashes_values, insertion_mask, indices_vec, combined, sizeof(hash_value_t));

    // Load the keys into vector registers
    __m512i base_offsets = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
    __m512i keys_base    = _mm512_set1_epi64((uint64_t)keys);
    __m512i keys_vec     = _mm512_add_epi64(keys_base, _mm512_mullo_epi64(base_offsets, _mm512_set1_epi64(key_size)));
    // printf("keys_vec:         %s\n", zmm512_64b_to_str(keys_vec).c_str());

    // Gather the indices for scattertering
    _mm512_mask_i64scatter_epi64(keyps, insertion_mask, indices_vec, keys_vec, sizeof(void *));

    // Increment the offset only for the pending keys
    offset = _mm512_add_epi32(offset, _mm512_set1_epi32(1));

    // Set the mask to 0 for indices with successful insertions
    mask = _mm512_kandn(insertion_mask, mask);

    // If offset == capacity, set the mask to 0 to prevent further probing
    if (_mm512_mask_cmpeq_epi32_mask(mask, offset, _mm512_set1_epi32(capacity))) {
      mask = 0;
    }

    // Count the number of bits set to 1 in the mask
    pending = _cvtmask16_u32(_mm_popcnt_u32(mask));
  }

  size += MapVec8::VECTOR_SIZE;
}

// FIXME: this should return an array of ints indicating successful reads.
int MapVec8::get_vec(void *keys, int *values_out) const {
  // Create a mask with all bits set to 1.
  // This mask will be updated in each iteration of the loop, indicating the lanes that are still pending.
  __mmask8 mask = 0xff;

  // Offset vector for linear probing, starting at 0.
  __m512i offset = _mm512_setzero_si512();

  // Load the hashes into a vector register
  __m512i hashes_vec = hash_keys_vec(keys, key_size);
  // printf("hashes_vec: %s\n", zmm512_64b_to_str(hashes_vec).c_str());

  unsigned pending = MapVec8::VECTOR_SIZE;
  while (pending != 0) {
    // Add offset to hashes to get the current indices
    __m512i indices_vec = _mm512_add_epi64(hashes_vec, offset);

    // & capacity - 1 to get the indices within the capacity
    indices_vec = _mm512_mask_and_epi64(_mm512_set1_epi64(0xffffffffffffffff), mask, indices_vec, _mm512_set1_epi64(capacity - 1));
    // printf("indices_vec:      %s\n", zmm512_64b_to_str(indices_vec).c_str());

    // Selectively gather hashes and values with the mask
    __m512i map_hashes_values_vec = _mm512_mask_i64gather_epi64(_mm512_setzero_si512(), mask, indices_vec, hashes_values, sizeof(hash_value_t));

    // Mask the values out, get the hashes
    __m512i map_hashes_vec = _mm512_and_epi64(map_hashes_values_vec, _mm512_set1_epi64(0x00000000ffffffff));
    // printf("map_hashes_vec:   %s\n", zmm512_64b_to_str(map_hashes_vec).c_str());

    // Create a mask for lanes where hashes match
    __mmask16 not_empty_slot = _mm512_mask_cmpneq_epi64_mask(mask, map_hashes_vec, _mm512_set1_epi64(SPECIAL_NULL_HASH));
    __mmask16 hash_cmp       = _mm512_mask_cmpeq_epi64_mask(mask, map_hashes_vec, hashes_vec);
    __mmask16 match_mask     = _mm512_kand(not_empty_slot, hash_cmp);

    // If the slot is empty, we can stop probing for that lane.
    mask = _mm512_kand(not_empty_slot, mask);

    // Load the keys into vector registers
    __m512i base_offsets = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
    __m512i keysp_base   = _mm512_set1_epi64((uint64_t)keys);
    __m512i keysp_vec    = _mm512_add_epi64(keysp_base, _mm512_mullo_epi64(base_offsets, _mm512_set1_epi64(key_size)));
    // printf("keys_vec:         %s\n", zmm512_64b_to_str(keys_vec).c_str());

    // Load the keys from memory for the lanes where the match_mask is set
    __m512i target_keysp_vec = _mm512_mask_i64gather_epi64(_mm512_setzero_si512(), match_mask, indices_vec, keyps, sizeof(void *));

    for (unsigned bytes_compared = 0; bytes_compared < key_size; bytes_compared += 8) {
      if (key_size - bytes_compared >= 8) {
        // Compare the gathered keys with the input keys to confirm matches
        // Keys can be arbitrarily large, so we need to compare them 32b at a time.
        // Gather the next 32b of the keys for comparison
        __m512i keys_vec        = _mm512_mask_i64gather_epi64(_mm512_setzero_si512(), match_mask, keysp_vec, NULL, 1);
        __m512i target_keys_vec = _mm512_mask_i64gather_epi64(_mm512_setzero_si512(), match_mask, target_keysp_vec, NULL, 1);
        __mmask8 keys_match     = _mm512_cmpeq_epi64_mask(keys_vec, target_keys_vec);

        // Not ideal, we are bouncing from GPR to ZMM, but I don't see any other alternative.
        match_mask = _mm512_kand(match_mask, keys_match);
      } else {
        // Handle the last few bytes that are less than 4
        // This is more complex because we can't directly compare with SIMD instructions.
        // We would need to create masks for the remaining bytes and compare them manually.
        // For simplicity, we'll skip this part in this implementation.
        assert(false && "TODO: Handle remaining bytes in get_vec");
      }

      // Advance key pointers by 4 bytes for the next iteration
      keysp_vec        = _mm512_add_epi64(keysp_vec, _mm512_set1_epi64(4));
      target_keysp_vec = _mm512_add_epi64(target_keysp_vec, _mm512_set1_epi64(4));
    }

    // Load the values from the vector register to the output array for the lanes where the mask is set
    __m256i values_256vec = _mm512_cvtepi64_epi32(_mm512_srli_epi64(map_hashes_values_vec, 32));
    // printf("values_256vec:    %s\n", zmm256_32b_to_str(values_256vec).c_str());
    _mm256_mask_storeu_epi32((__m256i *)values_out, match_mask, values_256vec);

    // Increment the offset only for the pending keys
    offset = _mm512_add_epi32(offset, _mm512_set1_epi32(1));

    // Update pending count and loop offset for the next iteration
    mask = _mm512_kandn(match_mask, mask);

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

int MapVec8::get(void *key, int *value_out) const {
  const unsigned hash = khash(key, key_size);

  for (unsigned i = 0; i < capacity; ++i) {
    const unsigned index  = loop(hash + i, capacity);
    const hash_value_t vh = hashes_values[index];
    if (vh.hash != SPECIAL_NULL_HASH && vh.hash == hash) {
      if (keq(keyps[index], key, key_size)) {
        *value_out = vh.value;
        return 1;
      }
    }
  }

  return -1;
}

void MapVec8::put(void *key, int value) {
  const unsigned hash = khash(key, key_size);

  for (unsigned i = 0; i < capacity; ++i) {
    const unsigned index = loop(hash + i, capacity);
    hash_value_t &vh     = hashes_values[index];
    if (vh.hash == SPECIAL_NULL_HASH) {
      keyps[index] = key;
      vh.hash      = hash;
      vh.value     = value;

      ++size;

      // printf("Put key %p with hash 0x%08x at index 0x%04x\n", key, hash, index);
      break;
    }
  }
}

void MapVec8::erase(void *key) {
  const unsigned hash = khash(key, key_size);
  for (unsigned i = 0; i < capacity; ++i) {
    const unsigned index = loop(hash + i, capacity);
    hash_value_t &vh     = hashes_values[index];
    if (vh.hash != SPECIAL_NULL_HASH && vh.hash == hash) {
      if (keq(keyps[index], key, key_size)) {
        vh.hash = SPECIAL_NULL_HASH;
        --size;
        break;
      }
    }
  }
}

unsigned MapVec8::get_size() const { return size; }
