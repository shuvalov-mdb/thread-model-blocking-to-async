#include <chrono>
#include <iostream>
#include <memory>

#include <benchmark/benchmark.h>
#include <ostream>

#include "benchmarks/blocking_to_async_suite.h"

namespace blocking_to_async {
namespace testing {

static std::unique_ptr<MultithreadedWorkload> mtWorkload;

static Config config;

namespace {

void percentBlockingCustomArguments(benchmark::internal::Benchmark* b) {
    std::vector<int> threadCount{ 
        8, 16, 32, 44, 64, 80, 100, 200
    };
    // In percentages.
    std::vector<int> ratioOfTimeToBlock{50, 80, 95};
    std::vector<int> iterationsBeforeSleep{ 1 };

    for (int ratio : ratioOfTimeToBlock) {
        for (int iterations : iterationsBeforeSleep) {
            for (int threads : threadCount) {
                b->Args({ratio, iterations, threads});
            }
        }
    }
    b->Iterations(1000);
}

void BM_percentBlocking(benchmark::State& state) {
    ContinuousWorkload mainThreadWorkload;
    mainThreadWorkload.init(config);

    // Remove unblocked threads and pooled workload.
    mtWorkload->scaleNonBlockingWorkloadTo(0);
    mtWorkload->stopPooledWorkload();

    assert(state.range(0) >= 0 && state.range(0) <= 99);
    mtWorkload->resetBlockingWorkflowTo(
        state.range(2),
        (state.range(0) / 100.),  // Percentage into ratio.
        state.range(1));
    mtWorkload->resetStats();

    auto statsBefore = mtWorkload->getStats();
    std::cerr<<"before "<<statsBefore<<std::endl;
    for (auto _ : state) {
        mainThreadWorkload.unitOfWork();
    }
    auto statsAfter = mtWorkload->getStats().diff(statsBefore);
    std::cerr<<"after "<<statsAfter<<std::endl;
    state.counters["qps"] = statsAfter.qps();
    state.counters["Migrations"] = statsAfter.migrationsQps();
}

//BENCHMARK(BM_percentBlocking)->Apply(percentBlockingCustomArguments);

void BM_pooledBlocks(benchmark::State& state) {
    ContinuousWorkload mainThreadWorkload;
    mainThreadWorkload.init(config);

    // Remove unblocked threads and pooled workload.
    mtWorkload->scaleNonBlockingWorkloadTo(0);
    mtWorkload->stopPooledWorkload();
    mtWorkload->resetBlockingWorkflowTo(0, 0, 0);

    assert(state.range(0) >= 0 && state.range(0) <= 99);
    mtWorkload->startPooledWorkload(
        state.range(2), 
        (state.range(0) / 100.),  // Percentage into ratio.
        state.range(1));
    mtWorkload->resetStats();

    auto statsBefore = mtWorkload->getStats();
    std::cerr<<"before "<<statsBefore<<std::endl;
    for (auto _ : state) {
        mainThreadWorkload.unitOfWork();
    }
    auto statsAfter = mtWorkload->getStats().diff(statsBefore);
    std::cerr<<"after "<<statsAfter<<std::endl;
    state.counters["qps"] = statsAfter.qps();
    state.counters["Migrations"] = statsAfter.migrationsQps();
}

BENCHMARK(BM_pooledBlocks)->Apply(percentBlockingCustomArguments);

}  // namespace
}  // namespace testing
}  // namespace blocking_to_async

using blocking_to_async::testing::Calibration;
using blocking_to_async::testing::ContinuousWorkload;
using blocking_to_async::testing::MultithreadedWorkload;

int main(int argc, char** argv)
{
    {
        auto calibration = std::make_unique<Calibration>();
        blocking_to_async::testing::mtWorkload = std::make_unique<MultithreadedWorkload>(
            [] { 
                auto workload = std::make_unique<ContinuousWorkload>();
                workload->init(blocking_to_async::testing::config);
                return workload;
            }
        );
        // blocking_to_async::testing::config.optimalConcurrency =
        //     calibration->calibrate(
        //         blocking_to_async::testing::config, 
        //         blocking_to_async::testing::mtWorkload.get());
        blocking_to_async::testing::config.optimalConcurrency = { 16, 2300 };//tmp
    }

    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
}
