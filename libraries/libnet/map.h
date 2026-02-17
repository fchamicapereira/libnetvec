#pragma once

#include "map-util.h"

class Map {
private:
  int *busybits;
  void **keyps;
  unsigned *khs;
  int *chns;
  int *vals;
  unsigned capacity;
  unsigned size;
  unsigned key_size;

public:
  Map(unsigned capacity, unsigned key_size);
  ~Map();

  int get(void *key, int *value_out) const;
  void put(void *key, int value);
  void erase(void *key, void **trash);
  unsigned get_size() const;
};
