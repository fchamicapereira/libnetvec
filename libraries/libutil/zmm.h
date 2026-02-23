#pragma once

#include <string>
#include <format>
#include <array>
#include <sstream>

#include <immintrin.h>
#include <stdint.h>

inline std::string zmm512_64b_to_str(__m512i v) {
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

inline std::string zmm256_64b_to_str(__m256i v) {
  alignas(32) std::array<uint64_t, 4> ptrs;
  _mm256_store_si256((__m256i *)ptrs.data(), v);

  std::stringstream ss;
  ss << "[";
  for (int i = 3; i >= 0; --i) {
    ss << std::format("0x{:016x}", ptrs[i]);
    if (i != 0) {
      ss << ",";
    }
  }
  ss << "]";
  return ss.str();
}

inline std::string zmm512_32b_to_str(__m512i v) {
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

inline std::string zmm256_32b_to_str(__m256i v) {
  alignas(32) std::array<uint32_t, 8> ptrs;
  _mm256_store_si256((__m256i *)ptrs.data(), v);

  std::stringstream ss;
  ss << "[";
  for (int i = 7; i >= 0; --i) {
    ss << std::format("0x{:08x}", ptrs[i]);
    if (i != 0) {
      ss << ",";
    }
  }
  ss << "]";
  return ss.str();
}
