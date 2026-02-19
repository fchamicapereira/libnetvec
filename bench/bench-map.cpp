#include <libnet/map.h>
#include <libnetburst/mapvec.h>
#include <libbench/bench.h>
#include <libbench/random.h>

#include <unordered_map>
#include <array>
#include <format>

using namespace bench;

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
      : MapBench<N>(std::format("Ustd-Uni-{}-R", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

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

template <size_t N> class UstdUniformWrites : public MapBench<N> {
private:
  std::unordered_map<std::array<bytes_t, N>, int, key_hasher_t<N>> map;

public:
  UstdUniformWrites(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<N>(std::format("Ustd-Uni-{}-W", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final { MapBench<N>::setup(); }

  void run() override final {
    for (u64 i : this->key_queries) {
      map[this->keys_pool[i]] = static_cast<int>(i);
      Benchmark::increment_counter();
    }
  }
};

template <size_t N> class UstdUniformUpdates : public MapBench<N> {
private:
  std::unordered_map<std::array<bytes_t, N>, int, key_hasher_t<N>> map;

public:
  UstdUniformUpdates(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<N>(std::format("Ustd-Uni-{}-U", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final {
    MapBench<N>::setup();
    for (u64 i = 0; i < this->map_capacity; i++) {
      map[this->keys_pool[i]] = static_cast<int>(i);
    }
  }

  void run() override final {
    for (u64 i : this->key_queries) {
      map[this->keys_pool[i]] += 1;
      Benchmark::increment_counter();
    }
  }
};

template <size_t N> class UstdUniformReadWrites : public MapBench<N> {
private:
  std::unordered_map<std::array<bytes_t, N>, int, key_hasher_t<N>> map;

public:
  UstdUniformReadWrites(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<N>(std::format("Ustd-Uni-{}-RW", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity) {}

  void setup() override final { MapBench<N>::setup(); }

  void run() override final {
    for (u64 i : this->key_queries) {
      auto found_it = map.find(this->keys_pool[i]);
      if (found_it != map.end()) {
        int value = found_it->second;
        (void)value;
      } else {
        map[this->keys_pool[i]] = static_cast<int>(i);
      }
      Benchmark::increment_counter();
    }
  }
};

template <size_t N> class MapUniformReads : public MapBench<N> {
private:
  Map map;

public:
  MapUniformReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<N>(std::format("Map-Uni-{}-R", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity, N) {}

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

template <size_t N> class MapUniformWrites : public MapBench<N> {
private:
  Map map;

public:
  MapUniformWrites(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<N>(std::format("Map-Uni-{}-W", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity, N) {}

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

template <size_t N> class MapUniformUpdates : public MapBench<N> {
private:
  Map map;

public:
  MapUniformUpdates(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<N>(std::format("Map-Uni-{}-U", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity, N) {}

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
      value += 1;
      Benchmark::increment_counter();
    }
  }
};

template <size_t N> class MapUniformReadWrites : public MapBench<N> {
private:
  Map map;

public:
  MapUniformReadWrites(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapBench<N>(std::format("Map-Uni-{}-RW", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity, N) {}

  void setup() override final {}

  void run() override final {
    for (u64 i : this->key_queries) {
      void *key = static_cast<void *>(this->keys_pool[i].data());
      int value;
      if (map.get(key, &value)) {
        (void)value;
      } else {
        map.put(key, static_cast<int>(i));
      }
      Benchmark::increment_counter();
    }
  }
};

// =====================================================================================
//
//                                 MapVec benchmarks
//
// =====================================================================================

template <size_t N> class MapVecBench : public Benchmark {
protected:
  const u64 map_capacity;
  const u64 total_operations;

  RandomUniformEngine uniform_engine;
  std::vector<std::array<bytes_t, N * MapVec::VECTOR_SIZE>> keys_pool;
  std::vector<u64> key_queries;

public:
  MapVecBench(const std::string &_name, u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : Benchmark(_name), map_capacity(_map_capacity), total_operations(_total_operations), uniform_engine(random_seed, 0, 0xff),
        keys_pool(_map_capacity / MapVec::VECTOR_SIZE), key_queries(_total_operations) {
    assert(map_capacity > 0 && "map_capacity must be greater than 0");
    assert(N > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
    assert((map_capacity & (map_capacity - 1)) == 0 && "map_capacity must be a power of 2");
  }

  void setup() override {
    for (u64 i = 0; i < map_capacity; i += MapVec::VECTOR_SIZE) {
      for (bytes_t j = 0; j < N * MapVec::VECTOR_SIZE; j++) {
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

template <size_t N> class MapVecUniformReads : public MapVecBench<N> {
private:
  MapVec map;

public:
  MapVecUniformReads(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVecBench<N>(std::format("MapVec-Uni-{}-R", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity, N) {}

  void setup() override final {
    for (u64 i = 0; i < this->map_capacity / MapVec::VECTOR_SIZE; i++) {
      for (int j = 0; j < MapVec::VECTOR_SIZE; j++) {
        void *key_ptr = static_cast<void *>(static_cast<bytes_t *>(this->keys_pool[i].data()) + j * N);
        int value     = static_cast<int>(i + j);
        map.put(key_ptr, value);
      }
    }
  }

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool[key_query].data());
      int values[MapVec::VECTOR_SIZE];
      map.get_vec(keys, values);
      Benchmark::increment_counter(MapVec::VECTOR_SIZE);
    }
  }
};

template <size_t N> class MapVecUniformWrites : public MapVecBench<N> {
private:
  MapVec map;

public:
  MapVecUniformWrites(u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : MapVecBench<N>(std::format("MapVec-Uni-{}-W", _total_operations), random_seed, _map_capacity, _total_operations), map(_map_capacity, N) {
    assert(_total_operations % MapVec::VECTOR_SIZE == 0 && "total_operations must be a multiple of MapVec::VECTOR_SIZE");
  }

  void setup() override final {}

  void run() override final {
    for (u64 i = 0; i < this->key_queries.size(); i += MapVec::VECTOR_SIZE) {
      const u64 key_query = this->key_queries[i];
      void *keys          = static_cast<void *>(this->keys_pool[key_query].data());
      int values[MapVec::VECTOR_SIZE];
      for (int j = 0; j < MapVec::VECTOR_SIZE; j++) {
        values[j] = static_cast<int>(i + j);
      }
      map.put_vec(keys, values);
      Benchmark::increment_counter(MapVec::VECTOR_SIZE);
    }
  }
};

int main() {
  BenchmarkSuite suite;

  suite.add_benchmark(std::make_unique<UstdUniformReads<12>>(0, 65536, 1'600'000));
  suite.add_benchmark(std::make_unique<MapUniformReads<12>>(0, 65536, 1'600'000));
  suite.add_benchmark(std::make_unique<MapVecUniformReads<12>>(0, 65536, 1'600'000));

  suite.add_benchmark(std::make_unique<UstdUniformWrites<12>>(0, 262'144, 65536));
  suite.add_benchmark(std::make_unique<MapUniformWrites<12>>(0, 262'144, 65536));
  suite.add_benchmark(std::make_unique<MapVecUniformWrites<12>>(0, 262'144, 65536));

  // suite.add_benchmark(std::make_unique<UstdUniformUpdates<12>>(0, 65536, 1'600'000));
  // suite.add_benchmark(std::make_unique<MapUniformUpdates<12>>(0, 65536, 1'600'000));

  // suite.add_benchmark(std::make_unique<UstdUniformReadWrites<12>>(0, 1 << 20, 1'600'000));
  // suite.add_benchmark(std::make_unique<MapUniformReadWrites<12>>(0, 1 << 20, 1'600'000));

  suite.run_all();

  return 0;
}