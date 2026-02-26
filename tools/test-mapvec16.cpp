#include <libnetvec/mapvec16.h>
#include <libutil/types.h>
#include <libutil/random.h>

#include <array>
#include <assert.h>

#include "common.h"

template <size_t key_size> void test_puts(const unsigned capacity, const unsigned total_puts) {
  MapVec16<key_size> map1(capacity);
  MapVec16<key_size> map2(capacity);
  RandomUniformEngine keys_uniform_engine(0, 0, 0xff);
  RandomUniformEngine values_uniform_engine(0);

  keys_pool_t keys(key_size, capacity);
  keys.random_populate(keys_uniform_engine);

  for (int ops_done = 0; ops_done < total_puts; ops_done += MapVec16<key_size>::VECTOR_SIZE) {
    int values[MapVec16<key_size>::VECTOR_SIZE];

    for (int i = 0; i < MapVec16<key_size>::VECTOR_SIZE; i++) {
      values[i] = values_uniform_engine.generate();
      printf("Key %02d: ", i);
      for (int j = key_size - 1; j >= 0; j--) {
        printf("%02x", keys.get_key(ops_done + i)[j]);
      }
      printf("\n");
    }

    for (int i = 0; i < MapVec16<key_size>::VECTOR_SIZE; i++) {
      void *target_key = (void *)keys.get_key(ops_done + i);
      int value        = values[i];
      map1.put(target_key, value);

      int new_value = 0xDEADBEEF;
      int found     = map1.get(target_key, &new_value);
      assert_or_panic(found == 1, "Failed to find key in map1");
      assert_or_panic(new_value == value, "Value mismatch in map1 (expected %d, got %d)", value, new_value);
    }

    void *target_keys = (void *)keys.get_key(ops_done);
    map2.put_vec(target_keys, values);

    for (int i = 0; i < MapVec16<key_size>::VECTOR_SIZE; i++) {
      void *key     = (void *)keys.get_key(ops_done + i);
      int value     = values[i];
      int new_value = 0xDEADBEEF;
      int found     = map2.get(key, &new_value);
      assert_or_panic(found == 1, "Failed to find key %p in map2", key);
      assert_or_panic(new_value == value, "Value mismatch in map2 (expected %d, got %d)", value, new_value);
    }
  }
}

template <size_t key_size> void test_gets(const unsigned capacity, const unsigned total_gets) {
  MapVec16<key_size> map(capacity);
  RandomUniformEngine keys_uniform_engine(0, 0, 0xff);
  RandomUniformEngine values_uniform_engine(0);

  keys_pool_t keys(key_size, capacity);
  keys.random_populate(keys_uniform_engine);

  for (int ops_done = 0; ops_done < total_gets; ops_done += MapVec16<key_size>::VECTOR_SIZE) {
    int values[MapVec16<key_size>::VECTOR_SIZE];

    for (int i = 0; i < MapVec16<key_size>::VECTOR_SIZE; i++) {
      values[i] = values_uniform_engine.generate();
      printf("Key %02d: ", i);
      for (int j = key_size - 1; j >= 0; j--) {
        printf("%02x", keys.get_key(ops_done + i)[j]);
      }
      printf("\n");
    }

    for (int i = 0; i < MapVec16<key_size>::VECTOR_SIZE; i++) {
      void *key = (void *)keys.get_key(ops_done + i);
      map.put(key, values[i]);
    }

    int new_values[MapVec16<key_size>::VECTOR_SIZE];
    for (int i = 0; i < MapVec16<key_size>::VECTOR_SIZE; i++) {
      new_values[i] = 0;
    }

    void *target_keys = (void *)keys.get_key(ops_done);
    map.get_vec(target_keys, new_values);

    for (int i = 0; i < MapVec16<key_size>::VECTOR_SIZE; i++) {
      int value     = values[i];
      int new_value = new_values[i];
      assert_or_panic(new_value == value, "Value mismatch in map (expected %d, got %d)", value, new_value);
    }
  }
}

template <size_t key_size> void test_unsuccessful_gets(const unsigned capacity, const unsigned total_gets) {
  MapVec16<key_size> map(capacity);
  RandomUniformEngine keys_uniform_engine(0, 0, 0xff);
  RandomUniformEngine values_uniform_engine(0);

  keys_pool_t keys(key_size, capacity);
  keys.random_populate(keys_uniform_engine);

  for (int ops_done = 0; ops_done < total_gets; ops_done += MapVec16<key_size>::VECTOR_SIZE) {
    int values[MapVec16<key_size>::VECTOR_SIZE];

    for (int i = 0; i < MapVec16<key_size>::VECTOR_SIZE; i++) {
      values[i] = values_uniform_engine.generate();
      printf("Key %02d: ", i);
      for (int j = key_size - 1; j >= 0; j--) {
        printf("%02x", keys.get_key(ops_done + i)[j]);
      }
      printf("\n");
    }

    int new_values[MapVec16<key_size>::VECTOR_SIZE];
    for (int i = 0; i < MapVec16<key_size>::VECTOR_SIZE; i++) {
      new_values[i] = 0;
    }

    void *target_keys = (void *)keys.get_key(ops_done);
    map.get_vec(target_keys, new_values);
  }
}

int main() {
  test_puts<16>(65536, 16);
  test_puts<16>(32, 16);
  test_puts<16>(65536, 65536);
  test_gets<16>(65536, 16);
  test_gets<16>(65536, 32);
  test_gets<16>(65536, 65536);
  test_unsuccessful_gets<16>(65536, 16);
  return 0;
}