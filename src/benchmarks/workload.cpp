/**
 * @author Andrew Shuvalov
 * @brief Benchmark the overhead of context switches.
 * @date 2022-07-18
 */

#include "benchmarks/workload.h"

#include <cassert>

namespace blocking_to_async {
namespace testing {

MultithreadedWorkload::MultithreadedWorkload(
    std::function<std::unique_ptr<Workload>()> createCallback)
    : _createCallback(createCallback) {
}

void MultithreadedWorkload::scaleTo(int newThreadCount) {
    assert(newThreadCount >= 0);

    while (newThreadCount < _workloads.size()) {
        _workloads.pop_back();
    }

    while (newThreadCount > _workloads.size()) {
        auto workload = _createCallback();
        assert(workload);
        auto threadWorkload = std::make_unique<ThreadWorkload>(std::move(workload));
        _workloads.push_back(std::move(threadWorkload));
    }
}

Stats MultithreadedWorkload::getStats() const {
    Stats result;
    for (const auto& w : _workloads) {
        auto stats = w->getStats();
        result.appendConcurrent(stats);
    }
    return result;
}


MultithreadedWorkload::ThreadWorkload::ThreadWorkload(std::unique_ptr<Workload> workload)
    : _workload(std::move(workload)),
      _terminate(false) {
    assert(_workload);
    _thread = std::make_unique<std::thread>([this] {
        Stats localStats;
        auto start = std::chrono::high_resolution_clock::now();

        while (!_terminate.load(std::memory_order_relaxed)) {
            _workload->unitOfWork();
            if (++localStats.iterations < 10) {
                continue;
            }

            auto now = std::chrono::high_resolution_clock::now();
            localStats.duration =
                std::chrono::duration_cast<std::chrono::microseconds>(now - start);
            start = now;
            std::lock_guard<std::mutex> guard(_mutex);
            // Calculate decaying stats (like moving average).
            _stats.duration = (_stats.duration + localStats.duration * 2) / 3;
            _stats.iterations = (_stats.iterations + localStats.iterations * 2) / 3;
        }
    });
}

MultithreadedWorkload::ThreadWorkload::~ThreadWorkload() {
    terminate();
}

void MultithreadedWorkload::ThreadWorkload::terminate() {
    _terminate = true;
    if (_thread) {
        _thread->join();
        _thread.reset();
    }
}

}  // namespace testing
}  // namespace blocking_to_async 
