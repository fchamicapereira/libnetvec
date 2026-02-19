#pragma once

class MapVec {
public:
  static constexpr const unsigned VECTOR_SIZE = 16;

private:
  const unsigned capacity;
  const unsigned key_size;

  int *busybits;
  void **keyps;
  unsigned *khs;
  int *chns;
  int *vals;
  unsigned size;

public:
  MapVec(unsigned capacity, unsigned key_size);
  ~MapVec();

  int get_vec(void *keys, int *values) const;
  void put_vec(void *keys, int *values);
  void erase_vec(void *keys);

  int get(void *key, int *value_out) const;
  void put(void *key, int value);
  void erase(void *key);

  unsigned get_size() const;
};
