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

public:
  Benchmark(const std::string &_name) : name(_name) {}

  const std::string &get_name() const { return name; }

  virtual void setup()    = 0;
  virtual void run()      = 0;
  virtual void teardown() = 0;

  void start() { start_time = clock::now(); }

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

      // Output the result
      printf("%-35s\t%15ld ns\n", benchmark->get_name().c_str(), duration);
    }
  }
};

} // namespace bench