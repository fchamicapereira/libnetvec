#pragma once

class MapVec16 {
public:
  static constexpr const unsigned VECTOR_SIZE = 16;

private:
  const unsigned capacity;
  const unsigned key_size;

  int *busybits;
  void **keyps;
  unsigned *khs;
  int *vals;

  unsigned size;

public:
  MapVec16(unsigned capacity, unsigned key_size);
  ~MapVec16();

  // FIXME: this should return an array of ints indicating successful reads.
  int get_vec(void *keys, int *values_out) const;
  void put_vec(void *keys, int *values);
  void erase_vec(void *keys);

  int get(void *key, int *value_out) const;
  void put(void *key, int value);
  void erase(void *key);

  unsigned get_size() const;
};
