#include <libnet/map.h>
#include <libnetvec/mapvec16.h>
#include <libnetvec/mapvec16v2.h>
#include <libnetvec/mapvec8.h>
#include <libutil/random.h>

#include <unordered_map>
#include <array>
#include <format>
#include <vector>
#include <chrono>

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
    for (const auto &group : benchmarks_groups) {
      printf("%s\n", group.first.c_str());
      for (const auto &benchmark : group.second) {
        benchmark->setup();
        benchmark->start();
        benchmark->run();
        const time_ns_t duration = benchmark->stop();
        benchmark->teardown();

        const double ops_per_sec = static_cast<double>(benchmark->get_counter()) / (duration / 1'000'000'000.0);

        printf("  %-35s\t%15ld ns\t%15.0f ops/sec\n", benchmark->get_name().c_str(), duration, ops_per_sec);
      }
    }
  }
};

template <size_t N> class MapBench : public Benchmark {
protected:
  const u64 map_capacity;
  const u64 total_operations;

  RandomUniformEngine uniform_engine;
  std::vector<std::array<bytes_t, N>> keys_pool;
  std::vector<u64> key_queries;

public:
  MapBench(const std::string &_name, u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : Benchmark(_name), map_capacity(_map_capacity), total_operations(_total_operations), uniform_engine(random_seed, 0, 0xff), keys_pool(_map_capacity),
        key_queries(_total_operations) {
    assert(map_capacity > 0 && "map_capacity must be greater than 0");
    assert(N > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
    assert((map_capacity & (map_capacity - 1)) == 0 && "map_capacity must be a power of 2");
  }

  void setup() override {
    for (u64 i = 0; i < map_capacity; i++) {
      for (bytes_t j = 0; j < N; j++) {
        keys_pool[i][j] = static_cast<bytes_t>(uniform_engine.generate());
      }
    }
    for (u64 i = 0; i < total_operations; ++i) {
      const u64 random_index = uniform_engine.generate() % map_capacity;
      key_queries.push_back(random_index);
    }
  }

  void teardown() override {}
};

template <size_t N> struct key_hasher_t {
  size_t operator()(const std::array<bytes_t, N> &key) const {
    unsigned hash     = 0;
    void *kptr        = (void *)key.data();
    unsigned key_size = N * sizeof(bytes_t);
    while (key_size > 0) {
      if (key_size >= sizeof(unsigned int)) {
        hash = __builtin_ia32_crc32si(hash, *(unsigned int *)kptr);
        kptr = (unsigned int *)kptr + 1;
        key_size -= sizeof(unsigned int);
      } else {
        unsigned int c = *(unsigned char *)kptr;
        hash           = __builtin_ia32_crc32si(hash, c);
        kptr           = (unsigned char *)kptr + 1;
        key_size -= 1;
      }
    }
    return hash;
  }
};

template <size_t N> class UstdUniformReads : public MapBench<N> {
private:
  std::unordered_map<std::array<bytes_t, N>, int, key_hasher_t<N>> map;

public:
  UstdUniformReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<N>(std::format("uni-r-stdumap-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final {
    MapBench<N>::setup();
    for (u64 i = 0; i < this->map_capacity; i++) {
      map[this->keys_pool[i]] = static_cast<int>(i);
    }
  }

  void run() override final {
    for (u64 i : this->key_queries) {
      map.at(this->keys_pool.at(i));
      Benchmark::increment_counter();
    }
  }
};

template <size_t N> class UstdUniformFailedReads : public MapBench<N> {
private:
  std::unordered_map<std::array<bytes_t, N>, int, key_hasher_t<N>> map;

public:
  UstdUniformFailedReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<N>(std::format("uni-fr-stdumap-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final { MapBench<N>::setup(); }

  void run() override final {
    for (u64 i : this->key_queries) {
      map.find(this->keys_pool.at(i));
      Benchmark::increment_counter();
    }
  }
};

template <size_t N> class UstdUniformWrites : public MapBench<N> {
private:
  std::unordered_map<std::array<bytes_t, N>, int, key_hasher_t<N>> map;

public:
  UstdUniformWrites(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<N>(std::format("uni-w-stdumap-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final { MapBench<N>::setup(); }

  void run() override final {
    for (u64 i : this->key_queries) {
      map[this->keys_pool[i]] = static_cast<int>(i);
      Benchmark::increment_counter();
    }
  }
};

template <size_t N> class MapUniformReads : public MapBench<N> {
private:
  Map map;

public:
  MapUniformReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<N>(std::format("uni-r-map-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity, N) {}

  void setup() override final {
    for (u64 i = 0; i < this->map_capacity; i++) {
      void *key_ptr = static_cast<void *>(this->keys_pool[i].data());
      int value     = static_cast<int>(i);
      map.put(key_ptr, value);
    }
  }

  void run() override final {
    for (u64 i : this->key_queries) {
      void *key = static_cast<void *>(this->keys_pool[i].data());
      int value;
      map.get(key, &value);
      Benchmark::increment_counter();
    }
  }
};

template <size_t N> class MapUniformFailedReads : public MapBench<N> {
private:
  Map map;

public:
  MapUniformFailedReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<N>(std::format("uni-fr-map-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity, N) {}

  void setup() override final {}

  void run() override final {
    for (u64 i : this->key_queries) {
      void *key = static_cast<void *>(this->keys_pool[i].data());
      int value;
      map.get(key, &value);
      Benchmark::increment_counter();
    }
  }
};

template <size_t N> class MapUniformWrites : public MapBench<N> {
private:
  Map map;

public:
  MapUniformWrites(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<N>(std::format("uni-w-map-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity, N) {}

  void setup() override final {}

  void run() override final {
    for (u64 i : this->key_queries) {
      void *key = static_cast<void *>(this->keys_pool[i].data());
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

template <size_t N> class MapVec16Bench : public Benchmark {
protected:
  const u64 map_capacity;
  const u64 total_operations;

  RandomUniformEngine uniform_engine;
  std::vector<std::array<bytes_t, N * MapVec16<N>::VECTOR_SIZE>> keys_pool;
  std::vector<u64> key_queries;

public:
  MapVec16Bench(const std::string &_name, u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : Benchmark(_name), map_capacity(_map_capacity), total_operations(_total_operations), uniform_engine(random_seed, 0, 0xff),
        keys_pool(_map_capacity / MapVec16<N>::VECTOR_SIZE), key_queries(_total_operations) {
    assert(map_capacity > 0 && "map_capacity must be greater than 0");
    assert(N > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
    assert((map_capacity & (map_capacity - 1)) == 0 && "map_capacity must be a power of 2");
  }

  void setup() override {
    for (u64 i = 0; i < map_capacity; i += MapVec16<N>::VECTOR_SIZE) {
      for (bytes_t j = 0; j < N * MapVec16<N>::VECTOR_SIZE; j++) {
        keys_pool[i][j] = static_cast<bytes_t>(uniform_engine.generate());
      }
    }
    for (u64 i = 0; i < total_operations; ++i) {
      const u64 random_index = uniform_engine.generate() % map_capacity;
      key_queries.push_back(random_index);
    }
  }

  void teardown() override {}
};

template <size_t N> class MapVec16UniformReads : public MapVec16Bench<N> {
private:
  MapVec16<N> map;

public:
  MapVec16UniformReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec16Bench<N>(std::format("uni-r-mapvec16-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final {
    for (u64 i = 0; i < this->map_capacity / MapVec16<N>::VECTOR_SIZE; i++) {
      for (int j = 0; j < MapVec16<N>::VECTOR_SIZE; j++) {
        void *key_ptr = static_cast<void *>(static_cast<bytes_t *>(this->keys_pool[i].data()) + j * N);
        int value     = static_cast<int>(i + j);
        map.put(key_ptr, value);
      }
    }
  }

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec16<N>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool[key_query].data());
      int values[MapVec16<N>::VECTOR_SIZE];
      map.get_vec(keys, values);
      Benchmark::increment_counter(MapVec16<N>::VECTOR_SIZE);
    }
  }
};

template <size_t N> class MapVec16UniformFailedReads : public MapVec16Bench<N> {
private:
  MapVec16<N> map;

public:
  MapVec16UniformFailedReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec16Bench<N>(std::format("uni-fr-mapvec16-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final {}

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec16<N>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool[key_query].data());
      int values[MapVec16<N>::VECTOR_SIZE];
      map.get_vec(keys, values);
      Benchmark::increment_counter(MapVec16<N>::VECTOR_SIZE);
    }
  }
};

template <size_t N> class MapVec16UniformWrites : public MapVec16Bench<N> {
private:
  MapVec16<N> map;

public:
  MapVec16UniformWrites(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec16Bench<N>(std::format("uni-w-mapvec16-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {
    assert(_total_operations % MapVec16<N>::VECTOR_SIZE == 0 && "total_operations must be a multiple of MapVec16<N>::VECTOR_SIZE");
  }

  void setup() override final {}

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec16<N>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool[key_query].data());
      int values[MapVec16<N>::VECTOR_SIZE];
      for (int j = 0; j < MapVec16<N>::VECTOR_SIZE; j++) {
        values[j] = static_cast<int>(i + j);
      }
      map.put_vec(keys, values);
      Benchmark::increment_counter(MapVec16<N>::VECTOR_SIZE);
    }
  }
};

// =====================================================================================
//
//                                 MapVec16v2 benchmarks
//
// =====================================================================================

template <size_t N> class MapVec16v2Bench : public Benchmark {
protected:
  const u64 map_capacity;
  const u64 total_operations;

  RandomUniformEngine uniform_engine;
  std::vector<std::array<bytes_t, N * MapVec16v2<N>::VECTOR_SIZE>> keys_pool;
  std::vector<u64> key_queries;

public:
  MapVec16v2Bench(const std::string &_name, u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : Benchmark(_name), map_capacity(_map_capacity), total_operations(_total_operations), uniform_engine(random_seed, 0, 0xff),
        keys_pool(_map_capacity / MapVec16v2<N>::VECTOR_SIZE), key_queries(_total_operations) {
    assert(map_capacity > 0 && "map_capacity must be greater than 0");
    assert(N > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
    assert((map_capacity & (map_capacity - 1)) == 0 && "map_capacity must be a power of 2");
  }

  void setup() override {
    for (u64 i = 0; i < map_capacity; i += MapVec16v2<N>::VECTOR_SIZE) {
      for (bytes_t j = 0; j < N * MapVec16v2<N>::VECTOR_SIZE; j++) {
        keys_pool[i][j] = static_cast<bytes_t>(uniform_engine.generate());
      }
    }
    for (u64 i = 0; i < total_operations; ++i) {
      const u64 random_index = uniform_engine.generate() % map_capacity;
      key_queries.push_back(random_index);
    }
  }

  void teardown() override {}
};

template <size_t N> class MapVec16v2UniformReads : public MapVec16v2Bench<N> {
private:
  MapVec16v2<N> map;

public:
  MapVec16v2UniformReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec16v2Bench<N>(std::format("uni-r-mapvec16v2-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final {
    for (u64 i = 0; i < this->map_capacity / MapVec16v2<N>::VECTOR_SIZE; i++) {
      for (int j = 0; j < MapVec16v2<N>::VECTOR_SIZE; j++) {
        void *key_ptr = static_cast<void *>(static_cast<bytes_t *>(this->keys_pool[i].data()) + j * N);
        int value     = static_cast<int>(i + j);
        map.put(key_ptr, value);
      }
    }
  }

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec16v2<N>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool[key_query].data());
      int values[MapVec16v2<N>::VECTOR_SIZE];
      map.get_vec(keys, values);
      Benchmark::increment_counter(MapVec16v2<N>::VECTOR_SIZE);
    }
  }
};

template <size_t N> class MapVec16v2UniformFailedReads : public MapVec16v2Bench<N> {
private:
  MapVec16v2<N> map;

public:
  MapVec16v2UniformFailedReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec16v2Bench<N>(std::format("uni-fr-mapvec16v2-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final {}

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec16v2<N>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool[key_query].data());
      int values[MapVec16v2<N>::VECTOR_SIZE];
      map.get_vec(keys, values);
      Benchmark::increment_counter(MapVec16v2<N>::VECTOR_SIZE);
    }
  }
};

template <size_t N> class MapVec16v2UniformWrites : public MapVec16v2Bench<N> {
private:
  MapVec16v2<N> map;

public:
  MapVec16v2UniformWrites(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec16v2Bench<N>(std::format("uni-w-mapvec16v2-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {
    assert(_total_operations % MapVec16v2<N>::VECTOR_SIZE == 0 && "total_operations must be a multiple of MapVec16v2<N>::VECTOR_SIZE");
  }

  void setup() override final {}

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec16v2<N>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool[key_query].data());
      int values[MapVec16v2<N>::VECTOR_SIZE];
      for (int j = 0; j < MapVec16v2<N>::VECTOR_SIZE; j++) {
        values[j] = static_cast<int>(i + j);
      }
      map.put_vec(keys, values);
      Benchmark::increment_counter(MapVec16v2<N>::VECTOR_SIZE);
    }
  }
};

// =====================================================================================
//
//                                 MapVec8 benchmarks
//
// =====================================================================================

template <size_t N> class MapVec8Bench : public Benchmark {
protected:
  const u64 map_capacity;
  const u64 total_operations;

  RandomUniformEngine uniform_engine;
  std::vector<std::array<bytes_t, N * MapVec8<N>::VECTOR_SIZE>> keys_pool;
  std::vector<u64> key_queries;

public:
  MapVec8Bench(const std::string &_name, u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : Benchmark(_name), map_capacity(_map_capacity), total_operations(_total_operations), uniform_engine(random_seed, 0, 0xff),
        keys_pool(_map_capacity / MapVec8<N>::VECTOR_SIZE), key_queries(_total_operations) {
    assert(map_capacity > 0 && "map_capacity must be greater than 0");
    assert(N > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
    assert((map_capacity & (map_capacity - 1)) == 0 && "map_capacity must be a power of 2");
  }

  void setup() override {
    for (u64 i = 0; i < map_capacity; i += MapVec8<N>::VECTOR_SIZE) {
      for (bytes_t j = 0; j < N * MapVec8<N>::VECTOR_SIZE; j++) {
        keys_pool[i][j] = static_cast<bytes_t>(uniform_engine.generate());
      }
    }
    for (u64 i = 0; i < total_operations; ++i) {
      const u64 random_index = uniform_engine.generate() % map_capacity;
      key_queries.push_back(random_index);
    }
  }

  void teardown() override {}
};

template <size_t N> class MapVec8UniformReads : public MapVec8Bench<N> {
private:
  MapVec8<N> map;

public:
  MapVec8UniformReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec8Bench<N>(std::format("uni-r-mapvec8-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final {
    for (u64 i = 0; i < this->map_capacity / MapVec8<N>::VECTOR_SIZE; i++) {
      for (int j = 0; j < MapVec8<N>::VECTOR_SIZE; j++) {
        void *key_ptr = static_cast<void *>(static_cast<bytes_t *>(this->keys_pool[i].data()) + j * N);
        int value     = static_cast<int>(i + j);
        map.put(key_ptr, value);
      }
    }
  }

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec8<N>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool[key_query].data());
      int values[MapVec8<N>::VECTOR_SIZE];
      map.get_vec(keys, values);
      Benchmark::increment_counter(MapVec8<N>::VECTOR_SIZE);
    }
  }
};

template <size_t N> class MapVec8UniformFailedReads : public MapVec8Bench<N> {
private:
  MapVec8<N> map;

public:
  MapVec8UniformFailedReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec8Bench<N>(std::format("uni-fr-mapvec8-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final {}

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec8<N>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool[key_query].data());
      int values[MapVec8<N>::VECTOR_SIZE];
      map.get_vec(keys, values);
      Benchmark::increment_counter(MapVec8<N>::VECTOR_SIZE);
    }
  }
};

template <size_t N> class MapVec8UniformWrites : public MapVec8Bench<N> {
private:
  MapVec8<N> map;

public:
  MapVec8UniformWrites(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVec8Bench<N>(std::format("uni-w-mapvec8-{}", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {
    assert(_total_operations % MapVec8<N>::VECTOR_SIZE == 0 && "total_operations must be a multiple of MapVec8<N>::VECTOR_SIZE");
  }

  void setup() override final {}

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec8<N>::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool[key_query].data());
      int values[MapVec8<N>::VECTOR_SIZE];
      for (int j = 0; j < MapVec8<N>::VECTOR_SIZE; j++) {
        values[j] = static_cast<int>(i + j);
      }
      map.put_vec(keys, values);
      Benchmark::increment_counter(MapVec8<N>::VECTOR_SIZE);
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