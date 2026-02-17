#include "map.h"
#include "map-impl-pow2.h"
#include "compute.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

Map::Map(unsigned _capacity, unsigned _key_size) {
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
  capacity = _capacity;
  size     = 0;
  key_size = _key_size;

  map_impl_init(busybits, key_size, keyps, khs, chns, vals, capacity);
}

Map::~Map() {
  free(busybits);
  free(keyps);
  free(khs);
  free(chns);
  free(vals);
}

static unsigned khash(void *key, unsigned key_size) {
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

int Map::get(void *key, int *value_out) const {
  unsigned hash = khash(key, key_size);
  return map_impl_get(busybits, keyps, khs, chns, vals, key, key_size, hash, value_out, capacity);
}

void Map::put(void *key, int value) {
  unsigned hash = khash(key, key_size);
  map_impl_put(busybits, keyps, khs, chns, vals, key, hash, value, capacity);
  ++size;
}

void Map::erase(void *key, void **trash) {
  unsigned hash = khash(key, key_size);
  map_impl_erase(busybits, keyps, khs, chns, key, key_size, hash, capacity, trash);
  --size;
}

unsigned Map::get_size() const { return size; }
