#include <cassert>
#include <ctime>
#include <cstdlib>
#include <iostream>
#include <random>

#include "benchmarks/continuous_workload.h"

#define MASK_16 ((1 << 16) - 1)

namespace blocking_to_async {
namespace testing {

void ContinuousWorkload::init(const Config& config) {
    _config = config;
    while (_data.size() < _config.dataSizePerThread) {
        // Keep the data fragmented.
        _data.resize((_data.size() + 2) * 1.1);
    }
}

void ContinuousWorkload::unitOfWork() {
    assert(!_data.empty());
    static thread_local std::mt19937 gen;
    std::uniform_int_distribution<std::mt19937::result_type> distrib(
        0, _data.size() - _config.memoryWorkSizePerIteration);

    uint64_t idx1 = distrib(gen);
    uint64_t idx2 = distrib(gen);

    for (int i = 0; i < _config.memoryWorkSizePerIteration; ++i, ++idx1, ++idx2) {
        auto& shuffled = _data[idx1];
        shuffled ^= shuffled << 7 & MASK_16;
        shuffled ^= shuffled >> 9;
        shuffled ^= shuffled << 8 & MASK_16;

        double number = (shuffled & MASK_16) * 3.14;
        _data[idx2] += *reinterpret_cast<uint64_t*>(&number);
    }
}

}  // namespace testing
}  // namespace blocking_to_async
