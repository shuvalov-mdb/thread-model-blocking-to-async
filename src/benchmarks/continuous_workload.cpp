#include <cassert>
#include <ctime>
#include <cstdlib>
#include <iostream>
#include <random>
#include <x86intrin.h>

#include "benchmarks/continuous_workload.h"

#define MASK_16 ((1 << 16) - 1)

namespace blocking_to_async {
namespace testing {

std::mutex ContinuousWorkload::_mutex;
std::deque<uint64_t>* ContinuousWorkload::_data;

static unsigned getCoreId() {
    unsigned id;
    __rdtscp(&id);
    return id;
}

void ContinuousWorkload::init(const Config& config) {
    _config = config;
    std::lock_guard<std::mutex> guard(_mutex);
    if (_data && _data->size() >= _config.sharedDataSize) {
        return;
    }
    if (!_data) { _data = new std::deque<uint64_t>; }
    _data->resize(_config.sharedDataSize / 1000);
    while (_data->size() < _config.sharedDataSize) {
        // Keep the data fragmented.
        _data->resize((_data->size() + 2) * 1.1);
    }
}

int ContinuousWorkload::unitOfWork() {
    assert(!_data->empty());
    static constexpr int kMemoryIterations = 20;
    static constexpr int kMemoryJump = 1024 * 32;
    int threadMigrations = 0;
    auto previousCoreId = getCoreId();
    static thread_local std::mt19937 gen;
    std::uniform_int_distribution<std::mt19937::result_type> distrib(
        0, _data->size() - _config.memoryWorkSizePerIteration * kMemoryJump);

    for (int memorySegment = 0; memorySegment < kMemoryIterations; ++memorySegment) {
        uint64_t idx1 = distrib(gen);
        uint64_t idx2 = distrib(gen);

        for (int i = 0; i < _config.memoryWorkSizePerIteration / kMemoryIterations;
            ++i, idx1 += kMemoryJump, idx2 += kMemoryJump) {
            assert(idx1 < _data->size());
            assert(idx2 < _data->size());
            auto& shuffled = (*_data)[idx1];
            shuffled ^= shuffled << 7 & MASK_16;
            shuffled ^= shuffled >> 9;
            shuffled ^= shuffled << 8 & MASK_16;

            double number = (shuffled & MASK_16) * 3.14;
            (*_data)[idx2] += *reinterpret_cast<uint64_t*>(&number);

            {
                // Artificial lock contention to increase the rate of context switches.
                static std::mutex mutex;
                static int counter;
                std::lock_guard<std::mutex> guard(mutex);
                ++counter;
            }

            auto currentCoreId = getCoreId();
            if (currentCoreId != previousCoreId) {
                previousCoreId = currentCoreId;
                ++threadMigrations;
            }
        }
    }

    return threadMigrations;
}

}  // namespace testing
}  // namespace blocking_to_async
