#pragma once

#include <functional>
#include <random>
#include <memory>
#include <cassert>
#include <iostream>

#include <libutil/types.h>

class RandomUniformEngine {
private:
  // What a mouth full...
  // This is the product of the bind expression below.
  using Generator = std::_Bind<std::uniform_int_distribution<i32>(std::mt19937)>;

  const u32 rand_seed;
  const std::mt19937 gen;
  const std::uniform_int_distribution<i32> random_dist;
  Generator generator;

public:
  RandomUniformEngine(u32 _rand_seed, i32 _min, i32 _max) : rand_seed(_rand_seed), gen(rand_seed), random_dist(_min, _max), generator(std::bind(random_dist, gen)) {}

  RandomUniformEngine(u32 _rand_seed) : rand_seed(_rand_seed), gen(rand_seed), random_dist(0, INT32_MAX), generator(std::bind(random_dist, gen)) {}

  RandomUniformEngine(const RandomUniformEngine &other)
      : rand_seed(other.rand_seed), gen(other.gen), random_dist(other.random_dist), generator(std::bind(random_dist, gen)) {}

  RandomUniformEngine(RandomUniformEngine &&other)
      : rand_seed(other.rand_seed), gen(std::move(other.gen)), random_dist(std::move(other.random_dist)), generator(std::move(other.generator)) {}

  i32 generate() { return generator(); }
  u32 get_seed() const { return rand_seed; }
  std::mt19937 get_engine() const { return gen; }
};

class SingletonRandomEngine : private RandomUniformEngine {
private:
  static std::unique_ptr<RandomUniformEngine> engine;

  SingletonRandomEngine(const SingletonRandomEngine &)            = delete;
  SingletonRandomEngine(SingletonRandomEngine &&)                 = delete;
  SingletonRandomEngine &operator=(const SingletonRandomEngine &) = delete;

public:
  static void seed(u32 rand_seed);
  static i32 generate();
};

class RandomRealEngine {
private:
  // What a mouth full...
  // This is the product of the bind expression below.
  using Generator = std::_Bind<std::uniform_real_distribution<>(std::mt19937)>;

  const u32 rand_seed;
  const std::mt19937 gen;
  const std::uniform_real_distribution<> random_dist;
  Generator generator;

public:
  RandomRealEngine(unsigned _rand_seed, double _min, double _max)
      : rand_seed(_rand_seed), gen(rand_seed), random_dist(_min, _max), generator(std::bind(random_dist, gen)) {}

  RandomRealEngine(unsigned _rand_seed) : rand_seed(_rand_seed), gen(rand_seed), random_dist(0, UINT64_MAX), generator(std::bind(random_dist, gen)) {}

  RandomRealEngine(const RandomRealEngine &other) : rand_seed(other.rand_seed), gen(other.gen), random_dist(other.random_dist), generator(std::bind(random_dist, gen)) {}

  RandomRealEngine(RandomRealEngine &&other)
      : rand_seed(other.rand_seed), gen(std::move(other.gen)), random_dist(std::move(other.random_dist)), generator(std::move(other.generator)) {}

  double generate() { return generator(); }
  unsigned get_seed() const { return rand_seed; }
};

class RandomZipfEngine {
private:
  RandomRealEngine rand;
  const double zipf_param;
  const u64 min;
  const u64 max;

public:
  RandomZipfEngine(unsigned _random_seed, double _zipf_param, u64 _min, u64 _max)
      : rand(_random_seed, 0, 1), zipf_param(fix_zipf_param(_zipf_param)), min(_min), max(_max) {}

  RandomZipfEngine(const RandomZipfEngine &other) : rand(other.rand), zipf_param(other.zipf_param), min(other.min), max(other.max) {}

  RandomZipfEngine(RandomZipfEngine &&other) : rand(std::move(other.rand)), zipf_param(other.zipf_param), min(other.min), max(other.max) {}

  // From Castan [SIGCOMM'18]
  // Source:
  // https://github.com/nal-epfl/castan/blob/master/scripts/pcap_tools/create_zipfian_distribution_pcap.py
  u64 generate() {
    assert(zipf_param > 0.0 && "zipf_param must be positive");
    assert(zipf_param != 1.0 && "zipf_param cannot be 1.0");
    const double probability = rand.generate();
    assert(probability >= 0 && probability <= 1 && "Invalid probability");

    const double p         = probability;
    const u64 N            = max - min + 1;
    const double s         = zipf_param;
    const double tolerance = 0.01;
    double x               = (double)N / 2.0;

    const double D = p * (12.0 * (pow(N, 1.0 - s) - 1) / (1.0 - s) + 6.0 - 6.0 * pow(N, -s) + s - pow(N, -1.0 - s) * s);

    while (true) {
      const double m    = pow(x, -2 - s);
      const double mx   = m * x;
      const double mxx  = mx * x;
      const double mxxx = mxx * x;

      const double a    = 12.0 * (mxxx - 1) / (1.0 - s) + 6.0 * (1.0 - mxx) + (s - (mx * s)) - D;
      const double b    = 12.0 * mxx + 6.0 * (s * mx) + (m * s * (s + 1.0));
      const double newx = std::max(1.0, x - a / b);

      if (std::abs(newx - x) <= tolerance) {
        const u64 i = min + newx - 1;
        assert(i >= min && i <= max && "Invalid index");
        return i;
      }

      x = newx;
    }
  }

private:
  static double fix_zipf_param(double zipf_param) {
    if (zipf_param == 0.0 || zipf_param == 1.0) {
      zipf_param += 1e-6;
    }
    return zipf_param;
  }
};
