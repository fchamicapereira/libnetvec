#include <libnet/map.h>
#include <libnetvec/mapvec16.h>
#include <libnetvec/mapvec16v2.h>
#include <libnetvec/mapvec8.h>
#include <libutil/random.h>
#include <libutil/hash.h>

#include <unordered_map>
#include <array>
#include <format>
#include <vector>
#include <chrono>

#include "common.h"

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
  void increment_counter(u64 increment = 1) { counter += increment; }

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
    for (const std::pair<std::string, BenchmarkSuite::benchmarks_t> &group : benchmarks_groups) {
      std::optional<time_ns_t> base_duration;
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

        const double ops_per_sec = static_cast<double>(benchmark->get_counter()) / (duration / 1'000'000'000.0);
        const double speedup     = static_cast<double>(*base_duration) / duration;

        printf("  %-25s", benchmark->get_name().c_str());
        printf("\t%15ld ns", duration);
        printf("\t%15.0f ops/sec", ops_per_sec);
        printf("\t\t%7.4fx speedup", speedup);
        printf("\n");
      }
    }
  }
};

template <size_t key_size> class MapBench : public Benchmark {
protected:
  const u64 map_capacity;
  const u64 total_operations;

  RandomUniformEngine uniform_engine;
  keys_pool_t keys_pool;
  std::vector<u64> key_queries;

public:
  MapBench(const std::string &_name, u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : Benchmark(_name), map_capacity(_map_capacity), total_operations(_total_operations), uniform_engine(random_seed, 0, 0xff), keys_pool(key_size, _map_capacity),
        key_queries(_total_operations) {
    assert(map_capacity > 0 && "map_capacity must be greater than 0");
    assert(key_size > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
    assert((map_capacity & (map_capacity - 1)) == 0 && "map_capacity must be a power of 2");
  }

  void setup() override {
    keys_pool.random_populate(uniform_engine);
    for (u64 i = 0; i < total_operations; ++i) {
      const u64 random_index = uniform_engine.generate() % map_capacity;
      key_queries.push_back(random_index);
    }
  }

  void teardown() override {}
};

template <size_t key_size> struct key_hasher_t {
  size_t operator()(const u8 *key) const { return crc32hash<key_size>(key); }
};

template <size_t key_size> class UstdUniformReads : public MapBench<key_size> {
private:
  std::unordered_map<u8 *, int, key_hasher_t<key_size>> map;

public:
  UstdUniformReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<key_size>(std::format("uni-r-stdumap-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final {
    MapBench<key_size>::setup();
    for (u64 i = 0; i < this->map_capacity; i++) {
      map[this->keys_pool.get_key(i)] = static_cast<int>(i);
    }
  }

  void run() override final {
    for (u64 i : this->key_queries) {
      map.at(this->keys_pool.get_key(i));
      Benchmark::increment_counter();
    }
  }
};

template <size_t key_size> class UstdUniformFailedReads : public MapBench<key_size> {
private:
  std::unordered_map<u8 *, int, key_hasher_t<key_size>> map;

public:
  UstdUniformFailedReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<key_size>(std::format("uni-fr-stdumap-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final { MapBench<key_size>::setup(); }

  void run() override final {
    for (u64 i : this->key_queries) {
      map.find(this->keys_pool.get_key(i));
      Benchmark::increment_counter();
    }
  }
};

template <size_t key_size> class UstdUniformWrites : public MapBench<key_size> {
private:
  std::unordered_map<u8 *, int, key_hasher_t<key_size>> map;

public:
  UstdUniformWrites(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<key_size>(std::format("uni-w-stdumap-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final { MapBench<key_size>::setup(); }

  void run() override final {
    for (u64 i : this->key_queries) {
      map[this->keys_pool.get_key(i)] = static_cast<int>(i);
      Benchmark::increment_counter();
    }
  }
};

template <size_t key_size> class MapUniformReads : public MapBench<key_size> {
private:
  Map map;

public:
  MapUniformReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<key_size>(std::format("uni-r-map-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity, key_size) {}

  void setup() override final {
    MapBench<key_size>::setup();
    for (u64 i = 0; i < this->map_capacity; i++) {
      void *key_ptr = static_cast<void *>(this->keys_pool.get_key(i));
      int value     = static_cast<int>(i);
      map.put(key_ptr, value);
    }
  }

  void run() override final {
    for (u64 i : this->key_queries) {
      void *key = static_cast<void *>(this->keys_pool.get_key(i));
      int value;
      map.get(key, &value);
      Benchmark::increment_counter();
    }
  }
};

template <size_t key_size> class MapUniformFailedReads : public MapBench<key_size> {
private:
  Map map;

public:
  MapUniformFailedReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<key_size>(std::format("uni-fr-map-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity, key_size) {}

  void setup() override final { MapBench<key_size>::setup(); }

  void run() override final {
    for (u64 i : this->key_queries) {
      void *key = static_cast<void *>(this->keys_pool.get_key(i));
      int value;
      map.get(key, &value);
      Benchmark::increment_counter();
    }
  }
};

template <size_t key_size> class MapUniformWrites : public MapBench<key_size> {
private:
  Map map;

public:
  MapUniformWrites(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<key_size>(std::format("uni-w-map-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity, key_size) {}

  void setup() override final { MapBench<key_size>::setup(); }

  void run() override final {
    for (u64 i : this->key_queries) {
      void *key = static_cast<void *>(this->keys_pool.get_key(i));
      int value = static_cast<int>(i);
      map.put(key, value);
      Benchmark::increment_counter();
    }
  }
};

// =====================================================================================
//
//                                 MapVec16 benchmarks
//
// =====================================================================================

template <size_t key_size> class MapVec16Bench : public Benchmark {
protected:
  const u64 map_capacity;
  const u64 total_operations;

  RandomUniformEngine uniform_engine;
  keys_pool_t keys_pool;
  std::vector<u64> key_queries;

public:
  MapVec16Bench(const std::string &_name, u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : Benchmark(_name), map_capacity(_map_capacity), total_operations(_total_operations), uniform_engine(random_seed, 0, 0xff), keys_pool(key_size, _map_capacity),
        key_queries(_total_operations) {
    assert(map_capacity > 0 && "map_capacity must be greater than 0");
    assert(key_size > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
    assert((map_capacity & (map_capacity - 1)) == 0 && "map_capacity must be a power of 2");
  }

  void setup() override {
    keys_pool.random_populate(uniform_engine);
    for (u64 i = 0; i < total_operations; ++i) {
      const u64 random_index = uniform_engine.generate() % (map_capacity - MapVec16<key_size>::VECTOR_SIZE);
      key_queries.push_back(random_index);
    }
  }

  void teardown() override {}
};

template <size_t key_size> class MapVec16UniformReads : public MapVec16Bench<key_size> {
private:
  MapVec16<key_size> map;

public:
  MapVec16UniformReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec16Bench<key_size>(std::format("uni-r-mapvec16-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final {
    MapVec16Bench<key_size>::setup();
    for (u64 i = 0; i < this->map_capacity; i++) {
      void *key_ptr = static_cast<void *>(this->keys_pool.get_key(i));
      int value     = static_cast<int>(i);
      map.put(key_ptr, value);
    }
  }

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec16<key_size>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool.get_key(key_query));
      int values[MapVec16<key_size>::VECTOR_SIZE];
      map.get_vec(keys, values);
      Benchmark::increment_counter(MapVec16<key_size>::VECTOR_SIZE);
    }
  }
};

template <size_t key_size> class MapVec16UniformFailedReads : public MapVec16Bench<key_size> {
private:
  MapVec16<key_size> map;

public:
  MapVec16UniformFailedReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec16Bench<key_size>(std::format("uni-fr-mapvec16-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final { MapVec16Bench<key_size>::setup(); }

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec16<key_size>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool.get_key(key_query));
      int values[MapVec16<key_size>::VECTOR_SIZE];
      map.get_vec(keys, values);
      Benchmark::increment_counter(MapVec16<key_size>::VECTOR_SIZE);
    }
  }
};

template <size_t key_size> class MapVec16UniformWrites : public MapVec16Bench<key_size> {
private:
  MapVec16<key_size> map;

public:
  MapVec16UniformWrites(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec16Bench<key_size>(std::format("uni-w-mapvec16-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {
    assert(_total_operations % MapVec16<key_size>::VECTOR_SIZE == 0 && "total_operations must be a multiple of MapVec16<key_size>::VECTOR_SIZE");
  }

  void setup() override final { MapVec16Bench<key_size>::setup(); }

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec16<key_size>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool.get_key(key_query));
      int values[MapVec16<key_size>::VECTOR_SIZE];
      for (int j = 0; j < MapVec16<key_size>::VECTOR_SIZE; j++) {
        values[j] = static_cast<int>(i + j);
      }
      map.put_vec(keys, values);
      Benchmark::increment_counter(MapVec16<key_size>::VECTOR_SIZE);
    }
  }
};

// =====================================================================================
//
//                                 MapVec16v2 benchmarks
//
// =====================================================================================

template <size_t key_size> class MapVec16v2Bench : public Benchmark {
protected:
  const u64 map_capacity;
  const u64 total_operations;

  RandomUniformEngine uniform_engine;
  keys_pool_t keys_pool;
  std::vector<u64> key_queries;

public:
  MapVec16v2Bench(const std::string &_name, u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : Benchmark(_name), map_capacity(_map_capacity), total_operations(_total_operations), uniform_engine(random_seed, 0, 0xff), keys_pool(key_size, _map_capacity),
        key_queries(_total_operations) {
    assert(map_capacity > 0 && "map_capacity must be greater than 0");
    assert(key_size > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
    assert((map_capacity & (map_capacity - 1)) == 0 && "map_capacity must be a power of 2");
  }

  void setup() override {
    keys_pool.random_populate(uniform_engine);
    for (u64 i = 0; i < total_operations; ++i) {
      const u64 random_index = uniform_engine.generate() % (map_capacity - MapVec16v2<key_size>::VECTOR_SIZE);
      key_queries.push_back(random_index);
    }
  }

  void teardown() override {}
};

template <size_t key_size> class MapVec16v2UniformReads : public MapVec16v2Bench<key_size> {
private:
  MapVec16v2<key_size> map;

public:
  MapVec16v2UniformReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec16v2Bench<key_size>(std::format("uni-r-mapvec16v2-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final {
    MapVec16v2Bench<key_size>::setup();
    for (u64 i = 0; i < this->map_capacity; i++) {
      void *key_ptr = static_cast<void *>(this->keys_pool.get_key(i));
      int value     = static_cast<int>(i);
      map.put(key_ptr, value);
    }
  }

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec16v2<key_size>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool.get_key(key_query));
      int values[MapVec16v2<key_size>::VECTOR_SIZE];
      map.get_vec(keys, values);
      Benchmark::increment_counter(MapVec16v2<key_size>::VECTOR_SIZE);
    }
  }
};

template <size_t key_size> class MapVec16v2UniformFailedReads : public MapVec16v2Bench<key_size> {
private:
  MapVec16v2<key_size> map;

public:
  MapVec16v2UniformFailedReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec16v2Bench<key_size>(std::format("uni-fr-mapvec16v2-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final { MapVec16v2Bench<key_size>::setup(); }

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec16v2<key_size>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool.get_key(key_query));
      int values[MapVec16v2<key_size>::VECTOR_SIZE];
      map.get_vec(keys, values);
      Benchmark::increment_counter(MapVec16v2<key_size>::VECTOR_SIZE);
    }
  }
};

template <size_t key_size> class MapVec16v2UniformWrites : public MapVec16v2Bench<key_size> {
private:
  MapVec16v2<key_size> map;

public:
  MapVec16v2UniformWrites(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec16v2Bench<key_size>(std::format("uni-w-mapvec16v2-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {
    assert(_total_operations % MapVec16v2<key_size>::VECTOR_SIZE == 0 && "total_operations must be a multiple of MapVec16v2<key_size>::VECTOR_SIZE");
  }

  void setup() override final { MapVec16v2Bench<key_size>::setup(); }

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec16v2<key_size>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool.get_key(key_query));
      int values[MapVec16v2<key_size>::VECTOR_SIZE];
      for (int j = 0; j < MapVec16v2<key_size>::VECTOR_SIZE; j++) {
        values[j] = static_cast<int>(i + j);
      }
      map.put_vec(keys, values);
      Benchmark::increment_counter(MapVec16v2<key_size>::VECTOR_SIZE);
    }
  }
};

// =====================================================================================
//
//                                 MapVec8 benchmarks
//
// =====================================================================================

template <size_t key_size> class MapVec8Bench : public Benchmark {
protected:
  const u64 map_capacity;
  const u64 total_operations;

  RandomUniformEngine uniform_engine;
  keys_pool_t keys_pool;
  std::vector<u64> key_queries;

public:
  MapVec8Bench(const std::string &_name, u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : Benchmark(_name), map_capacity(_map_capacity), total_operations(_total_operations), uniform_engine(random_seed, 0, 0xff), keys_pool(key_size, _map_capacity),
        key_queries(_total_operations) {
    assert(map_capacity > 0 && "map_capacity must be greater than 0");
    assert(key_size > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
    assert((map_capacity & (map_capacity - 1)) == 0 && "map_capacity must be a power of 2");
  }

  void setup() override {
    keys_pool.random_populate(uniform_engine);
    for (u64 i = 0; i < total_operations; ++i) {
      const u64 random_index = uniform_engine.generate() % (map_capacity - MapVec8<key_size>::VECTOR_SIZE);
      key_queries.push_back(random_index);
    }
  }

  void teardown() override {}
};

template <size_t key_size> class MapVec8UniformReads : public MapVec8Bench<key_size> {
private:
  MapVec8<key_size> map;

public:
  MapVec8UniformReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec8Bench<key_size>(std::format("uni-r-mapvec8-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final {
    MapVec8Bench<key_size>::setup();
    for (u64 i = 0; i < this->map_capacity; i++) {
      void *key_ptr = static_cast<void *>(this->keys_pool.get_key(i));
      int value     = static_cast<int>(i);
      map.put(key_ptr, value);
    }
  }

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec8<key_size>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool.get_key(key_query));
      int values[MapVec8<key_size>::VECTOR_SIZE];
      map.get_vec(keys, values);
      Benchmark::increment_counter(MapVec8<key_size>::VECTOR_SIZE);
    }
  }
};

template <size_t key_size> class MapVec8UniformFailedReads : public MapVec8Bench<key_size> {
private:
  MapVec8<key_size> map;

public:
  MapVec8UniformFailedReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec8Bench<key_size>(std::format("uni-fr-mapvec8-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final { MapVec8Bench<key_size>::setup(); }

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec8<key_size>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool.get_key(key_query));
      int values[MapVec8<key_size>::VECTOR_SIZE];
      map.get_vec(keys, values);
      Benchmark::increment_counter(MapVec8<key_size>::VECTOR_SIZE);
    }
  }
};

template <size_t key_size> class MapVec8UniformWrites : public MapVec8Bench<key_size> {
private:
  MapVec8<key_size> map;

public:
  MapVec8UniformWrites(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec8Bench<key_size>(std::format("uni-w-mapvec8-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {
    assert(_total_operations % MapVec8<key_size>::VECTOR_SIZE == 0 && "total_operations must be a multiple of MapVec8<key_size>::VECTOR_SIZE");
  }

  void setup() override final { MapVec8Bench<key_size>::setup(); }

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec8<key_size>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool.get_key(key_query));
      int values[MapVec8<key_size>::VECTOR_SIZE];
      for (int j = 0; j < MapVec8<key_size>::VECTOR_SIZE; j++) {
        values[j] = static_cast<int>(i + j);
      }
      map.put_vec(keys, values);
      Benchmark::increment_counter(MapVec8<key_size>::VECTOR_SIZE);
    }
  }
};

int main() {
  BenchmarkSuite suite;

  suite.add_benchmark_group("Uniform reads");
  suite.add_benchmark(std::make_unique<UstdUniformReads<16>>(0, 65536, 1'600'000));
  suite.add_benchmark(std::make_unique<MapUniformReads<16>>(0, 65536, 1'600'000));
  suite.add_benchmark(std::make_unique<MapVec16UniformReads<16>>(0, 65536, 1'600'000));
  suite.add_benchmark(std::make_unique<MapVec16v2UniformReads<16>>(0, 65536, 1'600'000));
  suite.add_benchmark(std::make_unique<MapVec8UniformReads<16>>(0, 65536, 1'600'000));

  suite.add_benchmark_group("Uniform failed reads");
  suite.add_benchmark(std::make_unique<UstdUniformFailedReads<16>>(0, 65536, 1'600'000));
  suite.add_benchmark(std::make_unique<MapUniformFailedReads<16>>(0, 65536, 1'600'000));
  suite.add_benchmark(std::make_unique<MapVec16UniformFailedReads<16>>(0, 65536, 1'600'000));
  suite.add_benchmark(std::make_unique<MapVec16v2UniformFailedReads<16>>(0, 65536, 1'600'000));
  suite.add_benchmark(std::make_unique<MapVec8UniformFailedReads<16>>(0, 65536, 1'600'000));

  suite.add_benchmark_group("Uniform writes");
  suite.add_benchmark(std::make_unique<UstdUniformWrites<16>>(0, 262'144, 65536));
  suite.add_benchmark(std::make_unique<MapUniformWrites<16>>(0, 262'144, 65536));
  suite.add_benchmark(std::make_unique<MapVec16UniformWrites<16>>(0, 262'144, 65536));
  suite.add_benchmark(std::make_unique<MapVec16v2UniformWrites<16>>(0, 262'144, 65536));
  suite.add_benchmark(std::make_unique<MapVec8UniformWrites<16>>(0, 262'144, 65536));

  suite.run_all();

  return 0;
}