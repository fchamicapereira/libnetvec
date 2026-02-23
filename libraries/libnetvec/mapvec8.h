#pragma once

class MapVec8 {
public:
  static constexpr const unsigned VECTOR_SIZE       = 8;
  static constexpr const unsigned SPECIAL_NULL_HASH = 0;

private:
  const unsigned capacity;
  const unsigned key_size;

  typedef struct {
    unsigned hash;
    int value;
  } hash_value_t;

  hash_value_t *hashes_values;
  void **keyps;

  unsigned size;

public:
  MapVec8(unsigned capacity, unsigned key_size);
  ~MapVec8();

  // FIXME: this should return an array of ints indicating successful reads.
  int get_vec(void *keys, int *values_out) const;
  void put_vec(void *keys, int *values);
  void erase_vec(void *keys);

  int get(void *key, int *value_out) const;
  void put(void *key, int value);
  void erase(void *key);

  unsigned get_size() const;
};
