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

public:
  Benchmark(const std::string &_name) : name(_name), counter(0) {}

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
    for (const auto &group : benchmarks_groups) {
      printf("%s\n", group.first.c_str());
      for (const auto &benchmark : group.second) {
        benchmark->setup();
        benchmark->start();
        benchmark->run();
        const time_ns_t duration = benchmark->stop();
        benchmark->teardown();

        const double ops_per_sec              = static_cast<double>(benchmark->get_counter()) / (duration / 1'000'000'000.0);
        const std::unordered_set<u32> &hashes = benchmark->get_generated_hashes();
        const size_t unique_hashes            = hashes.size();
        const size_t total_keys               = benchmark->get_counter();
        const double collision_rate           = total_keys > 0 ? (1.0 - static_cast<double>(unique_hashes) / total_keys) * 100.0 : 0.0;

        printf("  %-35s\t%15ld ns\t%15.0f ops/sec\t\t%5.2f%% collision rate\n", benchmark->get_name().c_str(), duration, ops_per_sec, collision_rate);
      }
    }
  }
};

template <size_t key_size> class CRC32Bench : public Benchmark {
private:
  const u64 total_operations;

  RandomUniformEngine uniform_engine;

public:
  CRC32Bench(u32 random_seed, u64 _total_operations)
      : Benchmark(std::format("crc32-{}", _total_operations)), total_operations(_total_operations), uniform_engine(random_seed, 0, 0xff) {
    assert(key_size > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
  }

  void setup() override final {}

  void run() override final {
    while (counter < total_operations) {
      std::array<bytes_t, key_size> key;
      for (bytes_t j = 0; j < key_size; j++) {
        key[j] = static_cast<bytes_t>(uniform_engine.generate());
      }
      const u32 hash = crc32hash<key_size>(key.data());
      store_hash(hash);
      increment_counter();
    }
  }

  void teardown() override final {}
};

template <size_t key_size> class FXHashBench : public Benchmark {
private:
  const u64 total_operations;

  RandomUniformEngine uniform_engine;

public:
  FXHashBench(u32 random_seed, u64 _total_operations)
      : Benchmark(std::format("fxhash-{}", _total_operations)), total_operations(_total_operations), uniform_engine(random_seed, 0, 0xff) {
    assert(key_size > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
  }

  void setup() override final {}

  void run() override final {
    while (counter < total_operations) {
      std::array<bytes_t, key_size> key;
      for (bytes_t j = 0; j < key_size; j++) {
        key[j] = static_cast<bytes_t>(uniform_engine.generate());
      }
      const u32 hash = fxhash<key_size>(key.data());
      store_hash(hash);
      increment_counter();
    }
  }

  void teardown() override final {}
};

template <size_t key_size> class FXHashVec8Bench : public Benchmark {
private:
  const u64 total_operations;

  RandomUniformEngine uniform_engine;

public:
  FXHashVec8Bench(u32 random_seed, u64 _total_operations)
      : Benchmark(std::format("fxhash-vec8-{}", _total_operations)), total_operations(_total_operations), uniform_engine(random_seed, 0, 0xff) {
    assert(key_size > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
  }

  void setup() override final {}

  void run() override final {
    while (counter < total_operations) {
      std::array<bytes_t, key_size * 8> key;
      for (bytes_t j = 0; j < key_size * 8; j++) {
        key[j] = static_cast<bytes_t>(uniform_engine.generate());
      }
      const __m256i hashes = fxhash_vec8<key_size>(key.data());
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

int main() {
  BenchmarkSuite suite;

  suite.add_benchmark_group("16B keys");
  suite.add_benchmark(std::make_unique<CRC32Bench<16>>(0, 1'000'000));
  suite.add_benchmark(std::make_unique<FXHashBench<16>>(0, 1'000'000));
  suite.add_benchmark(std::make_unique<FXHashVec8Bench<16>>(0, 1'000'000));

  suite.run_all();

  return 0;
}