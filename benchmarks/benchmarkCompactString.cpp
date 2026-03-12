#include <benchmark/benchmark.h>
#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <cstring>
#include "compactString.h"

// Generate random strings with specified characteristics
std::vector<std::string> generateRandomStrings(size_t count, size_t minLen = 5, size_t maxLen = 50) {
    std::vector<std::string> strings;
    strings.reserve(count);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> disLength(minLen, maxLen);
    std::uniform_int_distribution<> disChar(97, 122); // 'a' to 'z'

    for (size_t i = 0; i < count; ++i) {
        std::string str;
        size_t length = disLength(gen);
        str.reserve(length);
        for (size_t j = 0; j < length; ++j) {
            str += static_cast<char>(disChar(gen));
        }
        strings.push_back(str);
    }

    return strings;
}

std::vector<std::string> generateShortStrings(size_t count) {
    return generateRandomStrings(count, 1, 15);
}

std::vector<std::string> generateLongStrings(size_t count) {
    return generateRandomStrings(count, 16, 1000);
}

// ----------------------------------------------------------------------------
// Construction Benchmarks
// ----------------------------------------------------------------------------
static void BM_Construction_CompactString(benchmark::State& state) {
    const size_t size = state.range(0);
    const bool isShort = state.range(1) == 0;
    
    auto strings = isShort ? generateShortStrings(size) : generateLongStrings(size);

    for (auto _ : state) {
            std::vector<CompactString> compactStrings;
            compactStrings.reserve(size);
            for (const auto& str : strings) {
                compactStrings.emplace_back(str.c_str(), str.length());
            }
            benchmark::DoNotOptimize(compactStrings);
        }
}

static void BM_Construction_StdString(benchmark::State& state) {
    const size_t size = state.range(0);
    const bool isShort = state.range(1) == 0;
    
    auto strings = isShort ? generateShortStrings(size) : generateLongStrings(size);

    for (auto _ : state) {
            std::vector<std::string> stdStrings;
            stdStrings.reserve(size);
            for (const auto& str : strings) {
                stdStrings.push_back(str);
            }
            benchmark::DoNotOptimize(stdStrings);
        }
}

// ----------------------------------------------------------------------------
// Access Benchmarks
// ----------------------------------------------------------------------------
static void BM_Access_CompactString(benchmark::State& state) {
    const size_t count = state.range(0);
    const bool isShort = state.range(1) == 0;
    
    auto strings = isShort ? generateShortStrings(count) : generateLongStrings(count);
    std::vector<CompactString> compactStrings;
    compactStrings.reserve(count);
    for (const auto& str : strings) {
        compactStrings.emplace_back(str.c_str(), str.length());
    }

    for (auto _ : state) {
            size_t sum = 0;
            for (const auto& str : compactStrings) {
                sum += str.length();
                benchmark::DoNotOptimize(str.data());
            }
            benchmark::DoNotOptimize(sum);
        }
}

static void BM_Access_StdString(benchmark::State& state) {
    const size_t count = state.range(0);
    const bool isShort = state.range(1) == 0;
    
    auto strings = isShort ? generateShortStrings(count) : generateLongStrings(count);
    std::vector<std::string> stdStrings;
    stdStrings.reserve(count);
    for (const auto& str : strings) {
        stdStrings.push_back(str);
    }

    for (auto _ : state) {
            size_t sum = 0;
            for (const auto& str : stdStrings) {
                sum += str.length();
                benchmark::DoNotOptimize(str.data());
            }
            benchmark::DoNotOptimize(sum);
        }
}

// ----------------------------------------------------------------------------
// Modification Benchmarks
// ----------------------------------------------------------------------------
static void BM_Modification_CompactString(benchmark::State& state) {
    const size_t count = state.range(0);
    
    auto sourceStrings = generateShortStrings(count);
    auto modStrings = generateShortStrings(count);
    
    std::vector<CompactString> compactMaster;
    compactMaster.reserve(count);
    for (const auto& s : sourceStrings) {
        compactMaster.emplace_back(s.c_str(), s.length());
    }

    for (auto _ : state) {
            state.PauseTiming();
            // Manual deep copy because CompactString is not copyable
            std::vector<CompactString> compactTest;
            compactTest.reserve(count);
            for (const auto& item : compactMaster) {
                compactTest.emplace_back(item.data(), item.length());
            }
            state.ResumeTiming();

            for (size_t i = 0; i < count; ++i) {
                compactTest.data()[i].modify(modStrings[i].c_str(), modStrings[i].length());
            }
            benchmark::DoNotOptimize(compactTest);
        }
}

static void BM_Modification_StdString(benchmark::State& state) {
    const size_t count = state.range(0);
    
    auto sourceStrings = generateShortStrings(count);
    auto modStrings = generateShortStrings(count);
    
    std::vector<std::string> stdMaster = sourceStrings;

    for (auto _ : state) {
            state.PauseTiming();
            std::vector<std::string> stdTest = stdMaster;
            state.ResumeTiming();

            for (size_t i = 0; i < count; ++i) {
                stdTest[i] = modStrings[i];
            }
            benchmark::DoNotOptimize(stdTest);
        }
}

// ----------------------------------------------------------------------------
// Move Benchmarks
// ----------------------------------------------------------------------------
static void BM_Move_CompactString(benchmark::State& state) {
    const size_t count = state.range(0);
    
    auto strings = generateShortStrings(count);
    std::vector<CompactString> compactSourceMaster;
    compactSourceMaster.reserve(count);
    for (const auto& str : strings) {
        compactSourceMaster.emplace_back(str.c_str(), str.length());
    }

    for (auto _ : state) {
            state.PauseTiming();
            // Manual deep copy because CompactString is not copyable
            std::vector<CompactString> compactSource;
            compactSource.reserve(count);
            for (const auto& item : compactSourceMaster) {
                compactSource.emplace_back(item.data(), item.length());
            }

            std::vector<CompactString> compactDest;
            compactDest.reserve(count);
            state.ResumeTiming();

            for (auto& str : compactSource) {
                CompactString temp = std::move(str);
                compactDest.push_back(std::move(temp));
            }
            benchmark::DoNotOptimize(compactDest);
        }
}

static void BM_Move_StdString(benchmark::State& state) {
    const size_t count = state.range(0);
    
    auto strings = generateShortStrings(count);
    std::vector<std::string> stdSourceMaster = strings;

    for (auto _ : state) {
            state.PauseTiming();
            std::vector<std::string> stdSource = stdSourceMaster;
            std::vector<std::string> stdDest;
            stdDest.reserve(count);
            state.ResumeTiming();

            for (auto& str : stdSource) {
                std::string temp = std::move(str);
                stdDest.push_back(std::move(temp));
            }
            benchmark::DoNotOptimize(stdDest);
        }
}

// ----------------------------------------------------------------------------
// Memory Statistics
// ----------------------------------------------------------------------------
static void BM_Memory_Stats(benchmark::State& state) {
    const size_t count = 100000;
    
    // Setup
    auto shortStrings = generateShortStrings(count / 2);
    auto longStrings = generateLongStrings(count / 2);

    for (auto _ : state) {
            state.PauseTiming();
        
            std::vector<CompactString> compactStrings;
            compactStrings.reserve(count);
            for (const auto& str : shortStrings) {
                compactStrings.emplace_back(str.c_str(), str.length());
            }
            for (const auto& str : longStrings) {
                compactStrings.emplace_back(str.c_str(), str.length());
            }
        
            size_t compactFootprint = 0;
            for(const auto& str : compactStrings)
				compactFootprint += sizeof(CompactString) + str.capacity();
        

            std::vector<std::string> stdStrings;
            stdStrings.reserve(count);
            for (const auto& str : shortStrings) {
                stdStrings.push_back(str);
            }
            for (const auto& str : longStrings) {
                stdStrings.push_back(str);
            }

            size_t stdFootprint = 0;
            for (const auto& str : stdStrings) {
                stdFootprint += sizeof(std::string) + str.capacity();
            }
        
            state.counters["Compact(KB)"] = compactFootprint / 1024.0;
            state.counters["Std(KB)"] = stdFootprint / 1024.0;
            state.counters["Ratio(C/S)"] = static_cast<double>(compactFootprint) / stdFootprint;
        
            state.ResumeTiming();
        }
}

// Register Benchmarks

// Construction
BENCHMARK(BM_Construction_CompactString)
    ->Args({ 10000, 0 })->Args({ 100000, 0 })->Args({ 1000000, 0 })
    ->Args({ 10000, 1 })->Args({ 100000, 1 })->Args({ 1000000, 1 })
    ->Unit(benchmark::kMillisecond);
 

BENCHMARK(BM_Construction_StdString)
    ->Args({10000, 0})->Args({100000, 0})->Args({1000000, 0})
    ->Args({10000, 1})->Args({100000, 1})->Args({1000000, 1})
    ->Unit(benchmark::kMillisecond);

// Access
BENCHMARK(BM_Access_CompactString)
    ->Args({100000, 0})->Args({1000000, 0})
    ->Args({100000, 1})->Args({1000000, 1})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Access_StdString)
    ->Args({100000, 0})->Args({1000000, 0})
    ->Args({100000, 1})->Args({1000000, 1})
    ->Unit(benchmark::kMillisecond);

// Modification
BENCHMARK(BM_Modification_CompactString)->Args({100000})->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Modification_StdString)->Args({100000})->Unit(benchmark::kMillisecond);

// Move
BENCHMARK(BM_Move_CompactString)->Args({100000})->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Move_StdString)->Args({100000})->Unit(benchmark::kMillisecond);

// Memory
BENCHMARK(BM_Memory_Stats)->Iterations(1)->Unit(benchmark::kMillisecond);
