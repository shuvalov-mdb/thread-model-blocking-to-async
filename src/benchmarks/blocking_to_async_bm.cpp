#include <chrono>

#include <benchmark/benchmark.h>

#include "benchmarks/blocking_to_async_suite.h"

namespace blocking_to_async {
namespace {


}  // namespace
}  // namespace blocking_to_async

int main(int argc, char** argv)
{
   ::benchmark::Initialize(&argc, argv);
   ::benchmark::RunSpecifiedBenchmarks();
}
