#include <libnetburst/mapvec.h>
#include <libbench/types.h>
#include <libbench/random.h>

#include <array>
#include <assert.h>

#include "tests.h"

using bench::RandomUniformEngine;
using bench::u8;

void test_puts(const unsigned capacity, const unsigned key_size, const unsigned total_puts) {
  MapVec map1(capacity, key_size);
  MapVec map2(capacity, key_size);
  RandomUniformEngine uniform_engine(0, 0, 0xff);

  while (map2.get_size() < total_puts) {
    u8 keys[MapVec::VECTOR_SIZE * key_size];
    int values[MapVec::VECTOR_SIZE];

    for (int i = 0; i < MapVec::VECTOR_SIZE; i++) {
      // printf("Initializing key %5d (%p)\n", map2.get_size() + i, (void *)(&keys[i * key_size]));
      for (int j = 0; j < key_size; j++) {
        keys[i * key_size + j] = static_cast<u8>(uniform_engine.generate());
      }
    }

    for (int i = 0; i < MapVec::VECTOR_SIZE; i++) {
      values[i] = map2.get_size() + i;
    }

    for (int i = 0; i < MapVec::VECTOR_SIZE; i++) {
      void *key = (void *)(&keys[i * key_size]);
      int value = values[i];
      map1.put(key, value);

      int new_value = 0xDEADBEEF;
      int found     = map1.get(key, &new_value);
      assert_or_panic(found == 1, "Failed to find key in map1");
      assert_or_panic(new_value == value, "Value mismatch in map1 (expected %d, got %d)", value, new_value);
    }

    map2.put_vec(keys, values);

    for (int i = 0; i < MapVec::VECTOR_SIZE; i++) {
      void *key     = (void *)(&keys[i * key_size]);
      int value     = values[i];
      int new_value = 0xDEADBEEF;
      int found     = map2.get(key, &new_value);
      assert_or_panic(found == 1, "Failed to find key %p in map2", key);
      assert_or_panic(new_value == value, "Value mismatch in map2 (expected %d, got %d)", value, new_value);
    }
  }
}

void test_gets(const unsigned capacity, const unsigned key_size, const unsigned total_gets) {
  MapVec map(capacity, key_size);
  RandomUniformEngine uniform_engine(0, 0, 0xff);

  while (map.get_size() < total_gets) {
    u8 keys[MapVec::VECTOR_SIZE * key_size];
    int values[MapVec::VECTOR_SIZE];

    for (int i = 0; i < MapVec::VECTOR_SIZE; i++) {
      // printf("Initializing key %5d (%p)\n", map2.get_size() + i, (void *)(&keys[i * key_size]));
      for (int j = 0; j < key_size; j++) {
        keys[i * key_size + j] = static_cast<u8>(uniform_engine.generate());
      }
      values[i] = map.get_size() + i;
      void *key = (void *)(&keys[i * key_size]);
      map.put(key, values[i]);
    }

    int new_values[MapVec::VECTOR_SIZE];
    for (int i = 0; i < MapVec::VECTOR_SIZE; i++) {
      new_values[i] = 0;
    }
    map.get_vec(keys, new_values);

    for (int i = 0; i < MapVec::VECTOR_SIZE; i++) {
      int value     = values[i];
      int new_value = new_values[i];
      assert_or_panic(new_value == value, "Value mismatch in map (expected %d, got %d)", value, new_value);
    }
  }
}

void test_unsuccessful_gets(const unsigned capacity, const unsigned key_size, const unsigned total_gets) {
  MapVec map(capacity, key_size);
  RandomUniformEngine uniform_engine(0, 0, 0xff);

  for (unsigned op = 0; op < total_gets; op++) {
    u8 keys[MapVec::VECTOR_SIZE * key_size];
    int values[MapVec::VECTOR_SIZE];

    for (int i = 0; i < MapVec::VECTOR_SIZE; i++) {
      // printf("Initializing key %5d (%p)\n", map2.get_size() + i, (void *)(&keys[i * key_size]));
      for (int j = 0; j < key_size; j++) {
        keys[i * key_size + j] = static_cast<u8>(uniform_engine.generate());
      }
      values[i] = map.get_size() + i;
    }

    int new_values[MapVec::VECTOR_SIZE];
    for (int i = 0; i < MapVec::VECTOR_SIZE; i++) {
      new_values[i] = 0;
    }
    map.get_vec(keys, new_values);
  }
}

int main() {
  // test_puts(65536, 12, 16);
  // test_puts(32, 12, 32);
  // test_puts(65536, 12, 65536);
  // test_gets(65536, 12, 16);
  // test_gets(65536, 12, 32);
  // test_gets(65536, 12, 65536);
  test_unsuccessful_gets(65536, 12, 16);
  return 0;
}