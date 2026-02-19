#include "map.h"
#include "compute.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

Map::Map(unsigned _capacity, unsigned _key_size) : capacity(_capacity), key_size(_key_size), size(0) {
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

Map::~Map() {
  free(busybits);
  free(keyps);
  free(khs);
  free(chns);
  free(vals);
}

int Map::get(void *key, int *value_out) const {
  unsigned hash = khash(key, key_size);
  int index     = find_key(busybits, keyps, khs, chns, key, key_size, hash, capacity);

  if (-1 == index) {
    return 0;
  }

  *value_out = vals[index];
  return 1;
}

void Map::put(void *key, int value) {
  unsigned hash  = khash(key, key_size);
  unsigned start = loop(hash, capacity);
  unsigned index = find_empty(busybits, chns, start, capacity);

  busybits[index] = 1;
  keyps[index]    = key;
  khs[index]      = hash;
  vals[index]     = value;

  ++size;
}

void Map::erase(void *key) {
  unsigned hash = khash(key, key_size);
  find_key_remove_chain(busybits, keyps, khs, chns, key, key_size, hash, capacity);
  --size;
}

unsigned Map::get_size() const { return size; }
