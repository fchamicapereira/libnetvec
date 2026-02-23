#pragma once

#include <libutil/types.h>

#include <stdbool.h>

inline u64 ensure_power_of_two(u64 n) {
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;
  n++;
  return n;
}

inline bool is_power_of_two(u64 n) { return (n & (n - 1)) == 0; }

inline bool is_prime(u64 n) {
  if (n <= 1)
    return false;
  if (n <= 3)
    return true;

  // Slow but simple
  for (u64 i = 2; i < n; i++) {
    if (n % i == 0) {
      return false;
    }
  }

  return true;
}
