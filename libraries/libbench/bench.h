#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <memory>
#include <iostream>

#include <stdio.h>

#include <libbench/types.h>

namespace bench {

class Benchmark {
private:
  using clock = std::conditional<std::chrono::high_resolution_clock::is_steady, std::chrono::high_resolution_clock, std::chrono::steady_clock>::type;

  const std::string name;
  clock::time_point start_time;
  u64 counter;

public:
  Benchmark(const std::string &_name) : name(_name), counter(0) {}

  const std::string &get_name() const { return name; }
  u64 get_counter() const { return counter; }
  void increment_counter() { counter++; }

  virtual void setup()    = 0;
  virtual void run()      = 0;
  virtual void teardown() = 0;

  void start() {
    start_time = clock::now();
    counter    = 0;
  }

  time_ns_t stop() {
    const clock::time_point end_time = clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
  }
};

class BenchmarkSuite {
private:
  std::vector<std::unique_ptr<Benchmark>> benchmarks;

public:
  void add_benchmark(std::unique_ptr<Benchmark> benchmark) { benchmarks.push_back(std::move(benchmark)); }

  void run_all() {
    for (const auto &benchmark : benchmarks) {
      benchmark->setup();
      benchmark->start();
      benchmark->run();
      const time_ns_t duration = benchmark->stop();
      benchmark->teardown();

      const double ops_per_sec = static_cast<double>(benchmark->get_counter()) / (duration / 1'000'000'000.0);

      printf("%-35s\t%15ld ns\t%15.0f ops/sec\n", benchmark->get_name().c_str(), duration, ops_per_sec);
    }
  }
};

} // namespace bench