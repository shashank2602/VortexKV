#include <benchmark/benchmark.h>
#include "database.h"

#include <vector>
#include <string>
#include <random>
#include <optional>

// Generate random keys with variable lengths
// Distribution: 60% small (5-20), 30% medium (21-100), 10% large (101-500)
std::vector<std::string> generateRandomKeys(size_t count, size_t fixedLength = 0) {
    std::vector<std::string> keys;
    keys.reserve(count);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> charDis(97, 122); // 'a' to 'z'
    std::uniform_int_distribution<> categoryDis(1, 100);
    std::uniform_int_distribution<> smallLenDis(5, 20);
    std::uniform_int_distribution<> mediumLenDis(21, 100);
    std::uniform_int_distribution<> largeLenDis(101, 500);

    for (size_t i = 0; i < count; ++i) {
        size_t keyLength;
        if (fixedLength > 0) {
            keyLength = fixedLength;
        } else {
            int category = categoryDis(gen);
            if (category <= 60) {
                keyLength = smallLenDis(gen);
            } else if (category <= 90) {
                keyLength = mediumLenDis(gen);
            } else {
                keyLength = largeLenDis(gen);
            }
        }

        std::string key;
        key.reserve(keyLength);
        for (size_t j = 0; j < keyLength; ++j) {
            key += static_cast<char>(charDis(gen));
        }
        keys.push_back(std::move(key));
    }

    return keys;
}

// Generate random values with variable lengths
// Distribution: 40% small (10-50), 35% medium (51-500), 20% large (501-2000), 5% very large (2001-10000)
std::vector<std::string> generateRandomValues(size_t count, size_t fixedLength = 0) {
    std::vector<std::string> values;
    values.reserve(count);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> charDis(32, 126); // printable ASCII
    std::uniform_int_distribution<> categoryDis(1, 100);
    std::uniform_int_distribution<> smallLenDis(10, 50);
    std::uniform_int_distribution<> mediumLenDis(51, 500);
    std::uniform_int_distribution<> largeLenDis(501, 2000);
    std::uniform_int_distribution<> veryLargeLenDis(2001, 10000);

    for (size_t i = 0; i < count; ++i) {
        size_t valueLength;
        if (fixedLength > 0) {
            valueLength = fixedLength;
        } else {
            int category = categoryDis(gen);
            if (category <= 40) {
                valueLength = smallLenDis(gen);
            } else if (category <= 75) {
                valueLength = mediumLenDis(gen);
            } else if (category <= 95) {
                valueLength = largeLenDis(gen);
            } else {
                valueLength = veryLargeLenDis(gen);
            }
        }

        std::string value;
        value.reserve(valueLength);
        for (size_t j = 0; j < valueLength; ++j) {
            value += static_cast<char>(charDis(gen));
        }
        values.push_back(std::move(value));
    }

    return values;
}

// Helper to report both ops/sec and avg time per operation
void ReportStats(benchmark::State& state, size_t numOps) {
    //state.SetItemsProcessed(state.iterations() * numOps);
    state.counters["ops/sec"] = benchmark::Counter(
        numOps, benchmark::Counter::kIsIterationInvariantRate);
    state.counters["avg_time_ns"] = benchmark::Counter(
        numOps, benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
}

// ----------------------------------------------------------------------------
// SET Benchmarks
// ----------------------------------------------------------------------------
static void BM_Database_SET(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);
    auto values = generateRandomValues(size);

    std::optional<Database> db;
    for (auto _ : state) {
        state.PauseTiming();
        db.emplace();
        state.ResumeTiming();

        for (size_t i = 0; i < size; ++i) {
            db->SET(keys[i], values[i]);
        }

        state.PauseTiming();
        db.reset();
        state.ResumeTiming();
    }
    ReportStats(state, size);
}

static void BM_Database_SET_Integer(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);

    std::optional<Database> db;
    for (auto _ : state) {
        state.PauseTiming();
        db.emplace();
        state.ResumeTiming();

        for (size_t i = 0; i < size; ++i) {
            db->SET(keys[i], std::to_string(i));
        }

        state.PauseTiming();
        db.reset();
        state.ResumeTiming();
    }
    ReportStats(state, size);
}

static void BM_Database_SET_WithTTL(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);
    auto values = generateRandomValues(size);

    std::optional<Database> db;
    for (auto _ : state) {
        state.PauseTiming();
        db.emplace();
        state.ResumeTiming();

        for (size_t i = 0; i < size; ++i) {
            db->SET(keys[i], values[i], "10000");
        }

        state.PauseTiming();
        db.reset();
        state.ResumeTiming();
    }
    ReportStats(state, size);
}

// ----------------------------------------------------------------------------
// GET Benchmarks
// ----------------------------------------------------------------------------
static void BM_Database_GET(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);
    auto values = generateRandomValues(size);

    Database db;
    for (size_t i = 0; i < size; ++i) {
        db.SET(keys[i], values[i]);
    }

    for (auto _ : state) {
        for (const auto& key : keys) {
            auto result = db.GET(key);
            benchmark::DoNotOptimize(result);
        }
    }
    ReportStats(state, size);
}

static void BM_Database_GET_Miss(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);
    auto tempKeys = generateRandomKeys(size);
    
    // Add prefix to ensure these keys don't exist in the database
    std::vector<std::string> missKeys;
    missKeys.reserve(size);
    for (const auto& key : tempKeys) {
        missKeys.push_back("MISS_" + key);
    }

    Database db;
    for (size_t i = 0; i < size; ++i) {
        db.SET(keys[i], "value");
    }

    for (auto _ : state) {
        for (const auto& key : missKeys) {
            auto result = db.GET(key);
            benchmark::DoNotOptimize(result);
        }
    }
    ReportStats(state, size);
}

// ----------------------------------------------------------------------------
// DEL Benchmarks
// ----------------------------------------------------------------------------
static void BM_Database_DEL_Single(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);

    std::optional<Database> db;
    for (auto _ : state) {
        state.PauseTiming();
        db.emplace();
        for (size_t i = 0; i < size; ++i) {
            db->SET(keys[i], "value");
        }
        state.ResumeTiming();

        for (const auto& key : keys) {
            auto result = db->DEL(std::vector<std::string_view>{key});
            benchmark::DoNotOptimize(result);
        }

        state.PauseTiming();
        db.reset();
        state.ResumeTiming();
    }
    ReportStats(state, size);
}

static void BM_Database_DEL_Batch(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);

    std::vector<std::string_view> keyViews;
    keyViews.reserve(size);
    for (const auto& key : keys) {
        keyViews.push_back(key);
    }

    std::optional<Database> db;
    for (auto _ : state) {
        state.PauseTiming();
        db.emplace();
        for (size_t i = 0; i < size; ++i) {
            db->SET(keys[i], "value");
        }
        state.ResumeTiming();

        auto result = db->DEL(keyViews);
        benchmark::DoNotOptimize(result);

        state.PauseTiming();
        db.reset();
        state.ResumeTiming();
    }
    ReportStats(state, size);
}

// ----------------------------------------------------------------------------
// EXISTS Benchmarks
// ----------------------------------------------------------------------------
static void BM_Database_EXISTS(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);

    Database db;
    for (size_t i = 0; i < size; ++i) {
        db.SET(keys[i], "value");
    }

    std::vector<std::string_view> keyViews;
    keyViews.reserve(size);
    for (const auto& key : keys) {
        keyViews.push_back(key);
    }

    for (auto _ : state) {
        auto result = db.EXISTS(keyViews);
        benchmark::DoNotOptimize(result);
    }
    ReportStats(state, size);
}

// ----------------------------------------------------------------------------
// INCR/DECR Benchmarks
// ----------------------------------------------------------------------------
static void BM_Database_INCR(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);

    Database db;
    for (size_t i = 0; i < size; ++i) {
        db.SET(keys[i], "0");
    }

    for (auto _ : state) {
        for (const auto& key : keys) {
            auto result = db.INCR(key);
            benchmark::DoNotOptimize(result);
        }
    }
    ReportStats(state, size);
}

static void BM_Database_INCRBY(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);

    Database db;
    for (size_t i = 0; i < size; ++i) {
        db.SET(keys[i], "0");
    }

    for (auto _ : state) {
        for (const auto& key : keys) {
            auto result = db.INCRBY(key, "10");
            benchmark::DoNotOptimize(result);
        }
    }
    ReportStats(state, size);
}

// ----------------------------------------------------------------------------
// TTL Operations Benchmarks
// ----------------------------------------------------------------------------
static void BM_Database_EXPIRE(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);

    Database db;
    for (size_t i = 0; i < size; ++i) {
        db.SET(keys[i], "value");
    }

    for (auto _ : state) {
        for (const auto& key : keys) {
            auto result = db.EXPIRE(key, "10000");
            benchmark::DoNotOptimize(result);
        }
    }
    ReportStats(state, size);
}

static void BM_Database_TTL(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);

    Database db;
    for (size_t i = 0; i < size; ++i) {
        db.SET(keys[i], "value", "10000");
    }

    for (auto _ : state) {
        for (const auto& key : keys) {
            auto result = db.TTL(key);
            benchmark::DoNotOptimize(result);
        }
    }
    ReportStats(state, size);
}

static void BM_Database_PERSIST(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);

    std::optional<Database> db;
    for (auto _ : state) {
        state.PauseTiming();
        db.emplace();
        for (size_t i = 0; i < size; ++i) {
            db->SET(keys[i], "value", "10000");
        }
        state.ResumeTiming();

        for (const auto& key : keys) {
            auto result = db->PERSIST(key);
            benchmark::DoNotOptimize(result);
        }

        state.PauseTiming();
        db.reset();
        state.ResumeTiming();
    }
    ReportStats(state, size);
}

// ----------------------------------------------------------------------------
// Mixed Workload Benchmarks
// ----------------------------------------------------------------------------
static void BM_Database_MixedWorkload(benchmark::State& state) {
    const size_t size = state.range(0);
    auto keys = generateRandomKeys(size);
    auto values = generateRandomValues(size);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> opDist(0, 3);

    Database db;
    // Pre-populate with some data
    for (size_t i = 0; i < size / 2; ++i) {
        db.SET(keys[i], values[i]);
    }

    for (auto _ : state) {
        for (size_t i = 0; i < size; ++i) {
            int op = opDist(gen);
            switch (op) {
                case 0: {
                    auto result = db.SET(keys[i % size], values[i % size]);
                    benchmark::DoNotOptimize(result);
                    break;
                }
                case 1: {
                    auto result = db.GET(keys[i % size]);
                    benchmark::DoNotOptimize(result);
                    break;
                }
                case 2: {
                    auto result = db.EXISTS(std::vector<std::string_view>{keys[i % size]});
                    benchmark::DoNotOptimize(result);
                    break;
                }
                case 3: {
                    auto result = db.INCR(keys[i % size]);
                    benchmark::DoNotOptimize(result);
                    break;
                }
            }
        }
    }
    ReportStats(state, size);
}

// Register benchmarks
BENCHMARK(BM_Database_SET)->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_Database_SET_Integer)->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_Database_SET_WithTTL)->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_Database_GET)->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_Database_GET_Miss)->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_Database_DEL_Single)->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_Database_DEL_Batch)->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_Database_EXISTS)->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_Database_INCR)->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_Database_INCRBY)->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_Database_EXPIRE)->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_Database_TTL)->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_Database_PERSIST)->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_Database_MixedWorkload)->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);

BENCHMARK_MAIN();
