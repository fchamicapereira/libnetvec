#include <libnet/map.h>
#include <libbench/bench.h>
#include <libbench/random.h>

#include <unordered_map>
#include <array>

using namespace bench;

template <size_t N> class StdUnorderedMapBenchUniformRandomReads : public Benchmark {
private:
  struct key_hasher_t {
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

private:
  const u64 map_capacity;
  const u64 total_operations;

  RandomUniformEngine uniform_engine;
  std::unordered_map<std::array<bytes_t, N>, int, key_hasher_t> map;
  std::vector<std::array<bytes_t, N>> keys_pool;
  std::vector<u64> key_queries;

public:
  StdUnorderedMapBenchUniformRandomReads(const std::string &_name, u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : Benchmark(_name), map_capacity(_map_capacity), total_operations(_total_operations), uniform_engine(random_seed, 0, 0xff), map(_map_capacity),
        keys_pool(_map_capacity), key_queries(_total_operations) {
    assert(map_capacity > 0 && "map_capacity must be greater than 0");
    assert(N > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
    assert((map_capacity & (map_capacity - 1)) == 0 && "map_capacity must be a power of 2");
  }

  void setup() override final {
    for (u64 i = 0; i < map_capacity; i++) {
      for (bytes_t j = 0; j < N; j++) {
        keys_pool[i][j] = static_cast<bytes_t>(uniform_engine.generate());
      }
      map[keys_pool[i]] = static_cast<int>(i);
    }
    for (u64 i = 0; i < total_operations; ++i) {
      const u64 random_index = uniform_engine.generate() % map_capacity;
      key_queries.push_back(random_index);
    }
  }

  void run() override final {
    for (u64 i : key_queries) {
      const std::array<bytes_t, N> &key = keys_pool[i];
      int value;
      map.at(key);
    }
  }

  void teardown() override final {}
};

template <size_t N> class MapBenchUniformRandomReads : public Benchmark {
private:
  const u64 map_capacity;
  const u64 total_operations;

  RandomUniformEngine uniform_engine;
  Map map;
  std::vector<std::array<bytes_t, N>> keys_pool;
  std::vector<u64> key_queries;

public:
  MapBenchUniformRandomReads(const std::string &_name, u32 random_seed, u64 _map_capacity, u64 _total_operations)
      : Benchmark(_name), map_capacity(_map_capacity), total_operations(_total_operations), uniform_engine(random_seed, 0, 0xff),
        map(_map_capacity, N), keys_pool(_map_capacity), key_queries(_total_operations) {
    assert(map_capacity > 0 && "map_capacity must be greater than 0");
    assert(N > 0 && "key_size must be greater than 0");
    assert(total_operations > 0 && "total_operations must be greater than 0");
    assert((map_capacity & (map_capacity - 1)) == 0 && "map_capacity must be a power of 2");
  }

  void setup() override final {
    for (u64 i = 0; i < map_capacity; i++) {
      for (bytes_t j = 0; j < N; j++) {
        keys_pool[i][j] = static_cast<bytes_t>(uniform_engine.generate());
      }
      map.put((void *)keys_pool[i].data(), static_cast<int>(i));
    }
    for (u64 i = 0; i < total_operations; ++i) {
      const u64 random_index = uniform_engine.generate() % map_capacity;
      key_queries.push_back(random_index);
    }
  }

  void run() override final {
    for (u64 i : key_queries) {
      const std::array<bytes_t, N> &key = keys_pool[i];
      int value;
      map.get((void *)key.data(), &value);
    }
  }

  void teardown() override final {}
};

int main() {
  const u64 map_capacity     = 65536;
  const u64 total_operations = 1'000'000;

  BenchmarkSuite suite;
  suite.add_benchmark(std::make_unique<MapBenchUniformRandomReads<12>>("MapBenchUniformRandomReads", 0, map_capacity, total_operations));
  suite.add_benchmark(
      std::make_unique<StdUnorderedMapBenchUniformRandomReads<12>>("StdUnorderedMapBenchUniformRandomReads", 0, map_capacity, total_operations));
  suite.run_all();

  return 0;
}