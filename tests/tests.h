#pragma once

#include <string>
#include <cstdint>
#include <csignal>
#include <iostream>
#include <limits>
#include <cassert>
#include <filesystem>

inline void dbg_breakpoint() { raise(SIGTRAP); }

#define COLOR_RESET "\033[0m"
#define COLOR_BLACK "\033[30m"
#define COLOR_RED "\033[31m"
#define COLOR_RED_BRIGHT "\u001b[31;1m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN "\033[36m"
#define COLOR_WHITE ""
#define COLOR_BOLD "\033[1m"

#define panic(fmt, ...)                                                                                                                              \
  do {                                                                                                                                               \
    fprintf(stderr,                                                                                                                                  \
            "\n" COLOR_RED_BRIGHT fmt "\n"                                                                                                           \
            "%s@%s:%d"                                                                                                                               \
            "\n" COLOR_RESET,                                                                                                                        \
            ##__VA_ARGS__, __func__, __FILE__, __LINE__);                                                                                            \
    fflush(stderr);                                                                                                                                  \
    dbg_breakpoint();                                                                                                                                \
    exit(1);                                                                                                                                         \
  } while (0)

#define assert_or_panic(condition, fmt, ...)                                                                                                         \
  do {                                                                                                                                               \
    if (!(condition)) {                                                                                                                              \
      panic(fmt, ##__VA_ARGS__);                                                                                                                     \
    }                                                                                                                                                \
  } while (0)
