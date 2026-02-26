#include <libutil/hash.h>
#include <libutil/random.h>

#include "common.h"

template <size_t key_size> void test_fxhash8() {
  RandomUniformEngine uniform_engine(0, 0, 0xff);
  const hkey_vec8_t<key_size> keys = generate_random_key_vec8<key_size>(uniform_engine);

  std::array<u32, 8> hash_array;
  for (int i = 0; i < 8; i++) {
    hash_array[i] = fxhash<key_size>(keys.data() + i * key_size);
  }

  const __m256i hashes = fxhash_vec8<key_size>(keys.data());
  std::array<u32, 8> hash_vec;
  _mm256_storeu_si256((__m256i *)hash_vec.data(), hashes);

  for (int i = 0; i < 8; i++) {
    printf("Vec8 Hash for key %02d: 0x%08x\n", i, hash_vec[i]);
    assert_or_panic(hash_array[i] == hash_vec[i], "Hash mismatch between scalar and vec8 implementations");
  }
}

template <size_t key_size> void test_fxhash16() {
  RandomUniformEngine uniform_engine(0, 0, 0xff);
  const hkey_vec16_t<key_size> keys = generate_random_key_vec16<key_size>(uniform_engine);

  std::array<u32, 16> hash_array;
  for (int i = 0; i < 16; i++) {
    hash_array[i] = fxhash<key_size>(keys.data() + i * key_size);
  }

  const __m512i hashes = fxhash_vec16<key_size>(keys.data());
  std::array<u32, 16> hash_vec;
  _mm512_storeu_si512((__m512i *)hash_vec.data(), hashes);

  for (int i = 0; i < 16; i++) {
    printf("Vec16 Hash for key %02d: 0x%08x\n", i, hash_vec[i]);
    assert_or_panic(hash_array[i] == hash_vec[i], "Hash mismatch between scalar and vec16_64b implementations");
  }
}

int main() {
  test_fxhash8<16>();
  test_fxhash16<16>();
  return 0;
}