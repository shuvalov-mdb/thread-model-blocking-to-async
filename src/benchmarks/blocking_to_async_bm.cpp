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
        8, 10, 12, 16, 20, 24, 30, 36, 44
    };
    // In percentages.
    std::vector<int> ratioOfTimeToBlock{50, 80};
    std::vector<int> iterationsBeforeSleep{ 1 };

    for (int ratio : ratioOfTimeToBlock) {
        for (int iterations : iterationsBeforeSleep) {
            for (int threads : threadCount) {
std::cerr<<"threads "<<threads<<std::endl;
                b->Args({ratio, iterations, threads});
            }
        }
    }
}

void BM_percentBlocking(benchmark::State& state) {
    ContinuousWorkload mainThreadWorkload;
    mainThreadWorkload.init(config);

    // Concurrent workload will have 1/2 of all threads running unblocked.
    mtWorkload->scaleNonBlockingWorkloadTo(config.optimalConcurrency.threadCount / 4);

    mtWorkload->resetBlockingWorkflowTo(
        state.range(2),
        (state.range(0) / 100.),  // Percentage into ratio.
        state.range(1));
    mtWorkload->resetStats();

    for (auto _ : state) {
        mainThreadWorkload.unitOfWork();
    }
    auto statsAfter = mtWorkload->getStats();
    std::cerr<<"after "<<statsAfter<<std::endl;
    state.counters["qps"] = statsAfter.qps();
}

BENCHMARK(BM_percentBlocking)->Apply(percentBlockingCustomArguments);

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
