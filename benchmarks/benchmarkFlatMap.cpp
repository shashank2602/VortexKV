#include <benchmark/benchmark.h>
#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <unordered_map>
#include <numeric>

#include "flatMap.h"
#include "compactString.h"

// Generate random keys
std::vector<std::string> generateRandomKeys(size_t count, size_t keyLength = 10) {
    std::vector<std::string> keys;
    keys.reserve(count);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(97, 122); // 'a' to 'z'
    std::uniform_int_distribution<> disLength(5, 50);

    for (size_t i = 0; i < count; ++i) {
        std::string key;
        key.reserve(disLength(gen));
        for (size_t j = 0; j < keyLength; ++j) {
            key += static_cast<char>(dis(gen));
        }
        keys.push_back(key);
    }

    return keys;
}

// ----------------------------------------------------------------------------
// Insertion Benchmarks
// ----------------------------------------------------------------------------
static void BM_Insertion_UnorderedMap(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);

    for (auto _ : state) {
        std::unordered_map<std::string, int64_t> umap;
        for (const auto& key : keys) {
            umap[key] = 42LL;
        }
        benchmark::DoNotOptimize(umap);
    }
}

static void BM_Insertion_FlatMap(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);

    for (auto _ : state) {
        FlatMap<std::string, int64_t> flatmap;
        for (const auto& key : keys) {
            flatmap.insert(key, 42LL);
        }
        benchmark::DoNotOptimize(flatmap);
    }
}

static void BM_Insertion_FlatMapCompactString(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);

    for (auto _ : state) {
        FlatMap<CompactString, int64_t> flatmap;
        for (const auto& key : keys) {
            flatmap.insert(CompactString(key.c_str(), key.length()), 42LL);
        }
        benchmark::DoNotOptimize(flatmap);
    }
}

// ----------------------------------------------------------------------------
// Lookup Benchmarks
// ----------------------------------------------------------------------------
static void BM_Lookup_UnorderedMap(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);
    std::unordered_map<std::string, int64_t> umap;
    for (const auto& key : keys) {
        umap[key] = 42LL;
    }

    for (auto _ : state) {
        for (const auto& key : keys) {
            auto it = umap.find(key);
            benchmark::DoNotOptimize(it);
        }
    }
}

static void BM_Lookup_FlatMap(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);
    FlatMap<std::string, int64_t> flatmap;
    for (const auto& key : keys) {
        flatmap.insert(key, 42LL);
    }

    for (auto _ : state) {
        for (const auto& key : keys) {
            benchmark::DoNotOptimize(flatmap.find(key));
        }
    }
}

static void BM_Lookup_FlatMapCompactString(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);
    FlatMap<CompactString, int64_t> flatmap;
    for (const auto& key : keys) {
        flatmap.insert(CompactString(key.c_str(), key.length()), 42LL);
    }

    for (auto _ : state) {
        for (const auto& key : keys) {
            benchmark::DoNotOptimize(flatmap.find(key));
        }
    }
}

// ----------------------------------------------------------------------------
// Deletion Benchmarks
// ----------------------------------------------------------------------------
static void BM_Deletion_UnorderedMap(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);
    
    std::unordered_map<std::string, int64_t> master;
    for (const auto& key : keys) {
        master[key] = 42LL;
    }

    for (auto _ : state) {
        state.PauseTiming();
        std::unordered_map<std::string, int64_t> umap = master;
        state.ResumeTiming();

        for (const auto& key : keys) {
            umap.erase(key);
        }
        benchmark::DoNotOptimize(umap);
    }
}

static void BM_Deletion_FlatMap(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);
    
    FlatMap<std::string, int64_t> master;
    for (const auto& key : keys) {
        master.insert(key, 42LL);
    }

    for (auto _ : state) {
        state.PauseTiming();
        FlatMap<std::string, int64_t> flatmap = master;
        state.ResumeTiming();

        for (const auto& key : keys) {
            flatmap.remove(key);
        }
        benchmark::DoNotOptimize(flatmap);
    }
}

static void BM_Deletion_FlatMapCompactString(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);
    
    // Check if we can pre-build a master to copy from.
    // Since CompactString is not copyable, we cannot copy FlatMap<CompactString>.
    // We must rebuild it in the loop.

    for (auto _ : state) {
        state.PauseTiming();
        FlatMap<CompactString, int64_t> flatmap;
        for (const auto& key : keys) {
            flatmap.insert(CompactString(key.c_str(), key.length()), 42LL);
        }
        state.ResumeTiming();

        for (const auto& key : keys) {
            flatmap.remove(key);
        }
        benchmark::DoNotOptimize(flatmap);
    }
}

// ----------------------------------------------------------------------------
// Mixed Benchmarks
// ----------------------------------------------------------------------------
static void BM_Mixed_UnorderedMap(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);

    for (auto _ : state) {
        state.PauseTiming();
        std::unordered_map<std::string, int64_t> umap;
        state.ResumeTiming();

        for (size_t i = 0; i < keys.size(); ++i) {
            if (i % 3 == 0) {
                umap.erase(keys[i]);
            } else {
                umap[keys[i]] = static_cast<long long>(i);
            }
        }
        for (const auto& key : keys) {
             benchmark::DoNotOptimize(umap.find(key));
        }
    }
}

static void BM_Mixed_FlatMap(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);

    for (auto _ : state) {
        state.PauseTiming();
        FlatMap<std::string, int64_t> flatmap;
        state.ResumeTiming();

        for (size_t i = 0; i < keys.size(); ++i) {
            if (i % 3 == 0) {
                flatmap.remove(keys[i]);
            } else {
                flatmap.insert(keys[i], static_cast<long long>(i));
            }
        }
        for (const auto& key : keys) {
             benchmark::DoNotOptimize(flatmap.find(key));
        }
    }
}

static void BM_Mixed_FlatMapCompactString(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);

    for (auto _ : state) {
        state.PauseTiming();
        FlatMap<CompactString, int64_t> flatmap;
        state.ResumeTiming();

        for (size_t i = 0; i < keys.size(); ++i) {
            if (i % 3 == 0) {
                flatmap.remove(keys[i]);
            } else {
                flatmap.insert(CompactString(keys[i].c_str(), keys[i].length()), static_cast<long long>(i));
            }
        }
        for (const auto& key : keys) {
             benchmark::DoNotOptimize(flatmap.find(key));
        }
    }
}

static void BM_Lookup_Realistic(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size, 12); 

    using DBValue = std::variant<CompactString, long long>;
    FlatMap<CompactString, DBValue> storage;
    FlatMap<CompactString, long long> expirations;

    uint64_t seed = 42;
    storage.setHashSeed(seed);
    expirations.setHashSeed(seed);

    for (const auto& key : keys) {
        storage.insert(CompactString(key.c_str(), key.length()),
            CompactString("val", 3)); 
    }

    // Shuffle lookup order to simulate random access
    std::vector<size_t> indices(size);
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), std::mt19937(12345));

    for (auto _ : state) {
        for (size_t idx : indices) {
            const auto& key = keys[idx];
            uint32_t hash = storage.calculateHash(key.data(), key.size());
            auto* val = storage.find(key, hash, true);  // markVisited=true
            benchmark::DoNotOptimize(val);
            auto* exp = expirations.find(key, hash);
            benchmark::DoNotOptimize(exp);
        }
    }

    state.counters["ops"] = benchmark::Counter(size, benchmark::Counter::kIsIterationInvariantRate);
}

// ----------------------------------------------------------------------------
// Register Benchmarks
// ----------------------------------------------------------------------------

BENCHMARK(BM_Insertion_UnorderedMap)
    ->Args({1000})->Args({10000})->Args({100000})->Args({1000000})->Args({10000000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Insertion_FlatMap)
    ->Args({1000})->Args({10000})->Args({100000})->Args({1000000})->Args({10000000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Insertion_FlatMapCompactString)
    ->Args({1000})->Args({10000})->Args({100000})->Args({1000000})->Args({10000000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Lookup_UnorderedMap)
    ->Args({1000})->Args({10000})->Args({100000})->Args({1000000})->Args({10000000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Lookup_FlatMap)
    ->Args({1000})->Args({10000})->Args({100000})->Args({1000000})->Args({10000000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Lookup_FlatMapCompactString)
    ->Args({1000})->Args({10000})->Args({100000})->Args({1000000})->Args({10000000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Deletion_UnorderedMap)
    ->Args({1000})->Args({10000})->Args({100000})->Args({1000000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Deletion_FlatMap)
    ->Args({1000})->Args({10000})->Args({100000})->Args({1000000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Deletion_FlatMapCompactString)
    ->Args({1000})->Args({10000})->Args({100000})->Args({1000000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Mixed_UnorderedMap)
    ->Args({10000000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Mixed_FlatMap)
    ->Args({10000000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Mixed_FlatMapCompactString)
    ->Args({10000000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Lookup_Realistic)
    ->Args({ 1000000 })
    ->Unit(benchmark::kMillisecond);

