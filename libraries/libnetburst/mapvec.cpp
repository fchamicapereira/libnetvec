#include "mapvec.h"
#include "compute.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <immintrin.h>
#include <assert.h>

#include <iostream>
#include <format>
#include <sstream>

std::string zmm512_64b_to_str(__m512i v) {
  alignas(64) std::array<uint64_t, 8> ptrs;
  _mm512_store_si512(ptrs.data(), v);

  std::stringstream ss;
  ss << "[";
  for (int i = 7; i >= 0; --i) {
    ss << std::format("0x{:016x}", ptrs[i]);
    if (i != 0) {
      ss << ",";
    }
  }
  ss << "]";
  return ss.str();
}

std::string zmm512_32b_to_str(__m512i v) {
  alignas(64) std::array<uint32_t, 16> ptrs;
  _mm512_store_si512(ptrs.data(), v);

  std::stringstream ss;
  ss << "[";
  for (int i = 15; i >= 0; --i) {
    ss << std::format("0x{:08x}", ptrs[i]);
    if (i != 0) {
      ss << ",";
    }
  }
  ss << "]";
  return ss.str();
}

namespace {

inline int keq(void *key1, void *key2, unsigned keys_size) { return memcmp(key1, key2, keys_size) == 0; }

inline unsigned loop(unsigned k, unsigned capacity) { return k & (capacity - 1); }

inline int find_key(int *busybits, void **keyps, unsigned *k_hashes, int *chns, void *keyp, unsigned key_size, unsigned key_hash, unsigned capacity) {
  unsigned start = loop(key_hash, capacity);
  for (unsigned i = 0; i < capacity; ++i) {
    unsigned index = loop(start + i, capacity);
    int bb         = busybits[index];
    unsigned kh    = k_hashes[index];
    int chn        = chns[index];
    void *kp       = keyps[index];
    if (bb != 0 && kh == key_hash) {
      if (keq(kp, keyp, key_size)) {
        return (int)index;
      }
    } else {
      if (chn == 0) {
        return -1;
      }
    }
  }
  return -1;
}

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

inline unsigned find_empty(int *busybits, int *chns, unsigned start, unsigned capacity) {
  for (unsigned i = 0; i < capacity; ++i) {
    unsigned index = loop(start + i, capacity);
    int bb         = busybits[index];
    if (0 == bb) {
      return index;
    }
    int chn     = chns[index];
    chns[index] = chn + 1;
  }
  return -1;
}

inline unsigned find_key_remove_chain(int *busybits, void **keyps, unsigned *k_hashes, int *chns, void *keyp, unsigned key_size, unsigned key_hash,
                                      unsigned capacity) {
  unsigned i     = 0;
  unsigned start = loop(key_hash, capacity);

  for (; i < capacity; ++i) {
    unsigned index = loop(start + i, capacity);
    int bb         = busybits[index];
    unsigned kh    = k_hashes[index];
    int chn        = chns[index];
    void *kp       = keyps[index];
    if (bb != 0 && kh == key_hash) {
      if (keq(kp, keyp, key_size)) {
        busybits[index] = 0;
        return index;
      }
    }
    chns[index] = chn - 1;
  }
  return -1;
}

} // namespace

MapVec::MapVec(unsigned _capacity, unsigned _key_size) : capacity(_capacity), key_size(_key_size), size(0) {
  // Check that capacity is a power of 2
  if (_capacity == 0 || is_power_of_two(_capacity) == 0) {
    fprintf(stderr, "Error: Capacity must be a power of 2\n");
    exit(1);
  }

  busybits = (int *)malloc(sizeof(int) * (int)_capacity);
  keyps    = (void **)malloc(sizeof(void *) * (int)_capacity);
  khs      = (unsigned *)malloc(sizeof(unsigned) * (int)_capacity);
  chns     = (int *)malloc(sizeof(int) * (int)_capacity);
  vals     = (int *)malloc(sizeof(int) * (int)_capacity);

  for (unsigned i = 0; i < capacity; ++i) {
    busybits[i] = 0;
    chns[i]     = 0;
  }
}

MapVec::~MapVec() {
  free(busybits);
  free(keyps);
  free(khs);
  free(chns);
  free(vals);
}

void MapVec::put_vec(void *keys, int *values) {
  assert(size + MapVec::VECTOR_SIZE <= capacity);

  // TODO: vectorize this
  unsigned hashes[MapVec::VECTOR_SIZE];
  for (unsigned i = 0; i < MapVec::VECTOR_SIZE; ++i) {
    void *key = (void *)((unsigned char *)keys + i * key_size);
    hashes[i] = khash(key, key_size);
  }

  // Create a mask with all bits set to 1
  __mmask16 mask = 0xFFFF;

  // Offset vector for linear probing, starting at 0.
  __m512i offset = _mm512_setzero_si512();

  // Load the hashes into a vector register
  __m512i hashes_vec = _mm512_loadu_si512((void *)hashes);
  // printf("hashes_vec: %s\n", zmm512_32b_to_str(hashes_vec).c_str());

  unsigned pending = MapVec::VECTOR_SIZE;
  while (pending != 0) {
    // Add offset to hashes to get the current indices
    __m512i indices_vec = _mm512_add_epi32(hashes_vec, offset);

    // & capacity - 1 to get the indices within the capacity
    indices_vec = _mm512_and_epi32(indices_vec, _mm512_set1_epi32(capacity - 1));

    // printf("indices_vec: %s\n", zmm512_32b_to_str(indices_vec).c_str());

    // Selectively gather busybits using the mask
    __m512i busybits_vec = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), mask, indices_vec, busybits, sizeof(int));

    // Create a mask for indices where busybits is 0 (free slots)
    __mmask16 not_busybits_mask = _mm512_cmpeq_epi32_mask(busybits_vec, _mm512_setzero_si512());

    // std::cout << "Not busy: " << std::format("{:#08b}", not_busybits_mask) << "\n";

    // Use gather/scatter for detecting conflicts between lanes.
    // We could use VPCONFLICTD for this, but it's (1) actually quite heavy and (2) this way we can allow some of the conflicting keys to be inserted,
    // and let the rest be handled in the next iteration when they will no longer conflict.
    // Write temporary unique values to the hash array.
    __m512i lane_unique_constants_vec = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    _mm512_mask_i32scatter_epi32(khs, not_busybits_mask, indices_vec, lane_unique_constants_vec, sizeof(unsigned));
    __m512i gathered_lane_unique_constants_vec =
        _mm512_mask_i32gather_epi32(lane_unique_constants_vec, not_busybits_mask, indices_vec, khs, sizeof(unsigned));
    not_busybits_mask = _mm512_mask_cmpeq_epi32_mask(not_busybits_mask, gathered_lane_unique_constants_vec, lane_unique_constants_vec);

    // std::cout << "After conflict resolution:\nNot busy: " << std::format("{:#08b}", not_busybits_mask) << "\n";

    __mmask16 busybits_mask = _mm512_knot(not_busybits_mask);

    // Increment chns for the indices where busybits is not 0 (occupied slots)
    __m512i chns_vec = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), busybits_mask, indices_vec, chns, sizeof(int));
    chns_vec         = _mm512_add_epi32(chns_vec, _mm512_set1_epi32(1));
    _mm512_mask_i32scatter_epi32(chns, busybits_mask, indices_vec, chns_vec, sizeof(int));

    // Set busybits to 1 for the indices where busybits is 0 (empty slots)
    _mm512_mask_i32scatter_epi32(busybits, not_busybits_mask, indices_vec, _mm512_set1_epi32(1), sizeof(int));

    // Store the hashes, keys, and values for the indices where busybits is 0 (empty slots)
    _mm512_mask_i32scatter_epi32(khs, not_busybits_mask, indices_vec, hashes_vec, sizeof(unsigned));

    // Load the keys into vector registers, first 8 pointers (0-7) into the 'low' register and next 8 pointers (8-15) into the 'high' register
    __m512i base_offsets = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
    __m512i keys_base    = _mm512_set1_epi64((uint64_t)keys);
    __m512i keys_lo_vec  = _mm512_add_epi64(keys_base, _mm512_mullo_epi64(base_offsets, _mm512_set1_epi64(key_size)));
    keys_base            = _mm512_set1_epi64((uint64_t)keys + 8 * key_size);
    __m512i keys_hi_vec  = _mm512_add_epi64(keys_base, _mm512_mullo_epi64(base_offsets, _mm512_set1_epi64(key_size)));

    // printf("Keys[0]: %p\n", (unsigned char *)keys + 0 * key_size);
    // printf("Keys[8]: %p\n", (unsigned char *)keys + 8 * key_size);

    // Print keys_lo_vec and keys_hi_vec for debugging
    // printf("keys_lo_vec: %s\n", zmm512_64b_to_str(keys_lo_vec).c_str());
    // printf("keys_hi_vec: %s\n", zmm512_64b_to_str(keys_hi_vec).c_str());
    // printf("indices_vec: %s\n", zmm512_32b_to_str(indices_vec).c_str());

    __m256i indices_lo = _mm512_castsi512_si256(indices_vec);
    __m256i indices_hi = _mm512_extracti32x8_epi32(indices_vec, 1);

    _mm512_mask_i32scatter_epi64(keyps, not_busybits_mask, indices_lo, keys_lo_vec, sizeof(void *));
    _mm512_mask_i32scatter_epi64(keyps, not_busybits_mask >> 8, indices_hi, keys_hi_vec, sizeof(void *));

    _mm512_mask_i32scatter_epi32(vals, not_busybits_mask, indices_vec, _mm512_loadu_si512((void *)values), sizeof(int));

    // Set the mask to 0 for indices where busybits is 0 (empty slots)
    mask = _mm512_kand(busybits_mask, mask);

    // Increment the offset only for the pending keys
    offset = _mm512_mask_add_epi32(offset, mask, offset, _mm512_set1_epi32(1));

    // Count the number of bits set to 1 in the mask
    pending = _cvtmask16_u32(_mm_popcnt_u32(mask));
  }

  size += MapVec::VECTOR_SIZE;
}

int MapVec::get(void *key, int *value_out) const {
  unsigned hash = khash(key, key_size);
  int index     = find_key(busybits, keyps, khs, chns, key, key_size, hash, capacity);

  if (-1 == index) {
    return 0;
  }

  *value_out = vals[index];
  return 1;
}

void MapVec::put(void *key, int value) {
  unsigned hash  = khash(key, key_size);
  unsigned start = loop(hash, capacity);
  unsigned index = find_empty(busybits, chns, start, capacity);

  busybits[index] = 1;
  keyps[index]    = key;
  khs[index]      = hash;
  vals[index]     = value;

  ++size;

  // printf("Put key %p with hash 0x%08x at index 0x%04x\n", key, hash, index);
}

void MapVec::erase(void *key) {
  unsigned hash = khash(key, key_size);
  find_key_remove_chain(busybits, keyps, khs, chns, key, key_size, hash, capacity);
  --size;
}

unsigned MapVec::get_size() const { return size; }
