#include <libutil/hash.h>
#include <libutil/random.h>

#include <unordered_map>
#include <unordered_set>
#include <array>
#include <format>
#include <chrono>

class Benchmark {
protected:
  using clock = std::conditional<std::chrono::high_resolution_clock::is_steady, std::chrono::high_resolution_clock, std::chrono::steady_clock>::type;

  const std::string name;
  clock::time_point start_time;
  u64 counter;
  std::unordered_set<u32> generated_hashes;
  RandomUniformEngine uniform_engine;

public:
  Benchmark(const std::string &_name, u32 random_seed) : name(_name), counter(0), uniform_engine(random_seed, 0, 0xff) {}

  const std::string &get_name() const { return name; }
  u64 get_counter() const { return counter; }
  void increment_counter(u64 increment = 1) { counter += increment; }

  void store_hash(u32 hash) { generated_hashes.insert(hash); }
  const std::unordered_set<u32> &get_generated_hashes() const { return generated_hashes; }

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
  using benchmarks_t = std::vector<std::unique_ptr<Benchmark>>;

  std::vector<std::pair<std::string, benchmarks_t>> benchmarks_groups;

public:
  void add_benchmark(std::unique_ptr<Benchmark> benchmark) { benchmarks_groups.back().second.push_back(std::move(benchmark)); }
  void add_benchmark_group(const std::string &name) { benchmarks_groups.emplace_back(name, benchmarks_t{}); }

  void run_all() {
    std::optional<time_ns_t> base_duration;
    for (const std::pair<std::string, BenchmarkSuite::benchmarks_t> &group : benchmarks_groups) {
      printf("%s\n", group.first.c_str());
      for (const std::unique_ptr<Benchmark> &benchmark : group.second) {
        benchmark->setup();
        benchmark->start();
        benchmark->run();
        const time_ns_t duration = benchmark->stop();
        benchmark->teardown();

        if (!base_duration) {
          base_duration = duration;
        }

        const double ops_per_sec              = static_cast<double>(benchmark->get_counter()) / (duration / 1'000'000'000.0);
        const std::unordered_set<u32> &hashes = benchmark->get_generated_hashes();
        const size_t unique_hashes            = hashes.size();
        const size_t total_keys               = benchmark->get_counter();
        const double collision_rate           = total_keys > 0 ? (1.0 - static_cast<double>(unique_hashes) / total_keys) * 100.0 : 0.0;
        const double speedup                  = static_cast<double>(*base_duration) / duration;

        printf("  %-20s", benchmark->get_name().c_str());
        printf("\t%15ld ns", duration);
        printf("\t%15.0f ops/sec", ops_per_sec);
        printf("\t\t%5.2fx speedup", speedup);
        printf("\t\t%5.2f%% collision rate", collision_rate);
        printf("\n");
      }
    }
  }
};

template <size_t key_size> class CRC32_Bench : public Benchmark {
private:
  const u64 total_operations;

public:
  CRC32_Bench(u32 random_seed, u64 _total_operations) : Benchmark("crc32", random_seed), total_operations(_total_operations) {
    assert(key_size > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
  }

  void setup() override final {}

  void run() override final {
    while (counter < total_operations) {
      const hkey_t<key_size> key = generate_random_key<key_size>(uniform_engine);
      const u32 hash             = crc32hash<key_size>(key.data());
      store_hash(hash);
      increment_counter();
    }
  }

  void teardown() override final {}
};

template <size_t key_size> class FXHash_Bench : public Benchmark {
private:
  const u64 total_operations;

public:
  FXHash_Bench(u32 random_seed, u64 _total_operations) : Benchmark("fxhash", random_seed), total_operations(_total_operations) {
    assert(key_size > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
  }

  void setup() override final {}

  void run() override final {
    while (counter < total_operations) {
      const hkey_t<key_size> key = generate_random_key<key_size>(uniform_engine);
      const u32 hash             = fxhash<key_size>(key.data());
      store_hash(hash);
      increment_counter();
    }
  }

  void teardown() override final {}
};

template <size_t key_size> class FXHash_Vec8_Bench : public Benchmark {
private:
  const u64 total_operations;

public:
  FXHash_Vec8_Bench(u32 random_seed, u64 _total_operations) : Benchmark("fxhash-vec8", random_seed), total_operations(_total_operations) {
    assert(key_size > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
  }

  void setup() override final {}

  void run() override final {
    while (counter < total_operations) {
      const hkey_vec8_t<key_size> key = generate_random_key_vec8<key_size>(uniform_engine);
      const __m256i hashes            = fxhash_vec8<key_size>(key.data());
      std::array<u32, 8> hash_array;
      _mm256_storeu_si256((__m256i *)hash_array.data(), hashes);
      for (const u32 hash : hash_array) {
        store_hash(hash);
        increment_counter();
      }
    }
  }

  void teardown() override final {}
};

template <size_t key_size> class fxhash_vec16_Bench : public Benchmark {
private:
  const u64 total_operations;

public:
  fxhash_vec16_Bench(u32 random_seed, u64 _total_operations) : Benchmark("fxhash-vec16-64b", random_seed), total_operations(_total_operations) {
    assert(key_size > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
  }

  void setup() override final {}

  void run() override final {
    while (counter < total_operations) {
      const hkey_vec16_t<key_size> key = generate_random_key_vec16<key_size>(uniform_engine);
      const __m512i hashes             = fxhash_vec16<key_size>(key.data());
      std::array<u32, 16> hash_array;
      _mm512_storeu_si512((__m512i *)hash_array.data(), hashes);
      for (const u32 hash : hash_array) {
        store_hash(hash);
        increment_counter();
      }
    }
  }

  void teardown() override final {}
};

template <size_t key_size> class DJB2_Bench : public Benchmark {
private:
  const u64 total_operations;

public:
  DJB2_Bench(u32 random_seed, u64 _total_operations) : Benchmark("djb2", random_seed), total_operations(_total_operations) {
    assert(key_size > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
  }

  void setup() override final {}

  void run() override final {
    while (counter < total_operations) {
      const hkey_t<key_size> key = generate_random_key<key_size>(uniform_engine);
      const u32 hash             = djb2hash<key_size>(key.data());
      store_hash(hash);
      increment_counter();
    }
  }

  void teardown() override final {}
};

template <size_t key_size> class Murmur3_Bench : public Benchmark {
private:
  const u64 total_operations;

public:
  Murmur3_Bench(u32 random_seed, u64 _total_operations) : Benchmark("murmur3", random_seed), total_operations(_total_operations) {
    assert(key_size > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
  }

  void setup() override final {}

  void run() override final {
    while (counter < total_operations) {
      const hkey_t<key_size> key = generate_random_key<key_size>(uniform_engine);
      const u32 hash             = murmur3hash<key_size>(key.data());
      store_hash(hash);
      increment_counter();
    }
  }

  void teardown() override final {}
};

int main() {
  constexpr const size_t key_size = 16;
  const u32 seed                  = 0;
  const u32 N                     = 10'000'000;

  BenchmarkSuite suite;

  suite.add_benchmark_group("16B keys");
  suite.add_benchmark(std::make_unique<CRC32_Bench<key_size>>(seed, N));
  suite.add_benchmark(std::make_unique<FXHash_Bench<key_size>>(seed, N));
  suite.add_benchmark(std::make_unique<FXHash_Vec8_Bench<key_size>>(seed, N));
  suite.add_benchmark(std::make_unique<fxhash_vec16_Bench<key_size>>(seed, N));
  suite.add_benchmark(std::make_unique<DJB2_Bench<key_size>>(seed, N));
  suite.add_benchmark(std::make_unique<Murmur3_Bench<key_size>>(seed, N));

  suite.run_all();

  return 0;
}