#include <benchmark/benchmark.h>
#include "commandDispatcher.h"
#include "commands.h"
#include "database.h"
#include "linearBuffer.h"

#include <string>
#include <vector>


// ---------------------------------------------------------------------------
// Shared fixture helpers
// ---------------------------------------------------------------------------

static CommandDispatcher makeDispatcher()
{
    CommandDispatcher d;
    registerCommands(d);
    return d;
}

// Build a CommandRequest for a given type + args inline — no heap for ≤4 args
static CommandRequest makeCommand(std::string_view type, std::initializer_list<std::string_view> args = {})
{
    CommandRequest cmd;
    cmd.type = type;
    for (auto a : args)
        cmd.arguments.push_back(a);
    return cmd; // Use move semantics to avoid copy constructor
}


// ---------------------------------------------------------------------------
// BM_Dispatch_PING  — zero arg
// ---------------------------------------------------------------------------
static void BM_Dispatch_PING(benchmark::State& state)
{
    CommandDispatcher dispatcher = makeDispatcher();
    Database          db;
    LinearBuffer      responseBuffer;

    CommandRequest cmd = makeCommand("PING");

    for (auto _ : state) {
        cmd.arguments.clear();
        dispatcher.dispatch(cmd, db, responseBuffer);
        responseBuffer.reset();
        benchmark::DoNotOptimize(responseBuffer);
    }
    state.counters["ops/sec"] = benchmark::Counter(
        1, benchmark::Counter::kIsIterationInvariantRate);
}


// ---------------------------------------------------------------------------
// BM_Dispatch_GET  — most common command, single arg
// ---------------------------------------------------------------------------
static void BM_Dispatch_GET(benchmark::State& state)
{
    CommandDispatcher dispatcher = makeDispatcher();
    Database          db;
    LinearBuffer      responseBuffer;

    db.SET("key", "value");

    CommandRequest cmd = makeCommand("GET", {"key"});

    for (auto _ : state) {
        cmd.arguments.clear();
        cmd.arguments.push_back("key");
        dispatcher.dispatch(cmd, db, responseBuffer);
        responseBuffer.reset();
        benchmark::DoNotOptimize(responseBuffer);
    }
    state.counters["ops/sec"] = benchmark::Counter(
        1, benchmark::Counter::kIsIterationInvariantRate);
}


// ---------------------------------------------------------------------------
// BM_Dispatch_SET  — most common command, two args
// ---------------------------------------------------------------------------
static void BM_Dispatch_SET(benchmark::State& state)
{
    CommandDispatcher dispatcher = makeDispatcher();
    Database          db;
    LinearBuffer      responseBuffer;

    for (auto _ : state) {
        CommandRequest cmd = makeCommand("SET", {"key", "value"});
        dispatcher.dispatch(cmd, db, responseBuffer);
        responseBuffer.reset();
        benchmark::DoNotOptimize(responseBuffer);
    }
    state.counters["ops/sec"] = benchmark::Counter(
        1, benchmark::Counter::kIsIterationInvariantRate);
}


// ---------------------------------------------------------------------------
// BM_Dispatch_SET_EXPIRE  — most args 
// ---------------------------------------------------------------------------
static void BM_Dispatch_SET_EXPIRE(benchmark::State& state)
{
    CommandDispatcher dispatcher = makeDispatcher();
    Database          db;
    LinearBuffer      responseBuffer;

    db.SET("key", "value");

    for (auto _ : state) {
        CommandRequest cmd = makeCommand("EXPIRE", {"key", "1000", "PX", "1000"});
        dispatcher.dispatch(cmd, db, responseBuffer);
        responseBuffer.reset();
        benchmark::DoNotOptimize(responseBuffer);
    }
    state.counters["ops/sec"] = benchmark::Counter(
        1, benchmark::Counter::kIsIterationInvariantRate);
}


// ---------------------------------------------------------------------------
// BM_Dispatch_Unknown  — unknown command, returns error
// ---------------------------------------------------------------------------
static void BM_Dispatch_Unknown(benchmark::State& state)
{
    CommandDispatcher dispatcher = makeDispatcher();
    Database          db;
    LinearBuffer      responseBuffer;

    for (auto _ : state) {
        CommandRequest cmd = makeCommand("UNKNOWN", {"key"});
        dispatcher.dispatch(cmd, db, responseBuffer);
        responseBuffer.reset();
        benchmark::DoNotOptimize(responseBuffer);
    }
    state.counters["ops/sec"] = benchmark::Counter(
        1, benchmark::Counter::kIsIterationInvariantRate);
}



// ---------------------------------------------------------------------------
// BM_Dispatch_Pipeline  — simulates a realistic mix of commands in sequence
// ---------------------------------------------------------------------------
static void BM_Dispatch_Pipeline(benchmark::State& state)
{
    CommandDispatcher dispatcher = makeDispatcher();
    Database          db;
    LinearBuffer      responseBuffer;

    db.SET("k1", "100");

    struct PipelineEntry { std::string_view type; std::vector<std::string_view> args; };
    static const PipelineEntry pipeline[] = {
        { "SET",    { "k1", "hello" }     },
        { "GET",    { "k1" }              },
        { "INCR",   { "counter" }         },
        { "GET",    { "counter" }         },
        { "EXPIRE", { "k1", "3600" }      },
        { "TTL",    { "k1" }              },
        { "GET",    { "k1" }              },
        { "DEL",    { "k1" }              },
        { "GET",    { "k1" }              },
        { "PING",   {}                    },
    };

    for (auto _ : state) {
        for (const auto& entry : pipeline) {
            CommandRequest cmd;
            cmd.type = entry.type;
            for (auto a : entry.args)
                cmd.arguments.push_back(a);
            dispatcher.dispatch(cmd, db, responseBuffer);
        }
        responseBuffer.reset();
        benchmark::DoNotOptimize(responseBuffer);
    }

    state.counters["ops/sec"] = benchmark::Counter(
        std::size(pipeline), benchmark::Counter::kIsIterationInvariantRate);
}


BENCHMARK(BM_Dispatch_PING)->MinTime(2);
BENCHMARK(BM_Dispatch_GET)->MinTime(2);
BENCHMARK(BM_Dispatch_SET)->MinTime(2);
BENCHMARK(BM_Dispatch_SET_EXPIRE)->MinTime(2);
BENCHMARK(BM_Dispatch_Unknown)->MinTime(2);
BENCHMARK(BM_Dispatch_Pipeline)->MinTime(2);
