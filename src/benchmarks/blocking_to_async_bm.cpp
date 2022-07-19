#include <chrono>
#include <iostream>
#include <memory>

#include <benchmark/benchmark.h>
#include <ostream>

#include "benchmarks/blocking_to_async_suite.h"

namespace blocking_to_async {
namespace testing {

static std::unique_ptr<Calibration> calibration;

static std::unique_ptr<MultithreadedWorkload> mtWorkload;

static Config config;

namespace {


}  // namespace
}  // namespace testing
}  // namespace blocking_to_async

using blocking_to_async::testing::Calibration;
using blocking_to_async::testing::Config;
using blocking_to_async::testing::ContinuousWorkload;
using blocking_to_async::testing::MultithreadedWorkload;

int main(int argc, char** argv)
{
    blocking_to_async::testing::calibration = std::make_unique<Calibration>();
    blocking_to_async::testing::mtWorkload = std::make_unique<MultithreadedWorkload>(
        [] { 
            auto workload = std::make_unique<ContinuousWorkload>();
            workload->init(blocking_to_async::testing::config);
            return workload;
        }
    );
    blocking_to_async::testing::calibration->calibrate(
        blocking_to_async::testing::config, 
        blocking_to_async::testing::mtWorkload.get());
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
}