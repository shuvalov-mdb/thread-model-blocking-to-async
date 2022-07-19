/**
 * @author Andrew Shuvalov
 * @brief Benchmark the overhead of context switches.
 * @date 2022-07-18
 */

#include "benchmarks/workload.h"

#include <cassert>
#include <random>

namespace blocking_to_async {
namespace testing {

double Stats::qps() const {
    return iterations * 1000.0 * 1000 / duration.count();
}

// Increase iterations for the same interval.
void Stats::appendConcurrent(const Stats& other) {
    if (other.duration == std::chrono::microseconds{ 0 }) {
        return;
    }
    if (duration == std::chrono::microseconds{ 0 }) {
        *this = other;
        return;  // First time append into empty.
    }

    iterations += other.iterations * duration / other.duration;
}

void Stats::append(const Stats& other) {
    duration += other.duration;
    iterations += other.iterations;
}

Stats Stats::diff(const Stats& other) const {
    Stats result;
    result.duration = duration - other.duration;
    result.iterations = iterations - other.iterations;
    return result;
}

MultithreadedWorkload::MultithreadedWorkload(
    std::function<std::unique_ptr<Workload>()> createCallback)
    : _createCallback(createCallback) {
}

int MultithreadedWorkload::_removeExtraWorkloadsByType(int newThreadCount, bool isBlocking) {
    int countFound = 0;
    for (auto it = _workloads.begin(); it != _workloads.end();) {
        if ((*it)->isBlocking() == isBlocking) { 
            ++countFound; 
        } else {
            ++it;
            continue;
        }

        if (countFound > newThreadCount) {
            it = _workloads.erase(it);
            countFound = newThreadCount;
        } else {
            ++it;
        }
    }
    return countFound;
}

void MultithreadedWorkload::scaleNonBlockingWorkloadTo(int newThreadCount) {
    assert(newThreadCount >= 0);

    int remaining = _removeExtraWorkloadsByType(newThreadCount, false);

    for (int toAdd = newThreadCount - remaining; toAdd > 0; --toAdd) {
        auto workload = _createCallback();
        assert(workload);
        auto threadWorkload = std::make_unique<ThreadWorkload>(std::move(workload));
        threadWorkload->start();
        _workloads.push_back(std::move(threadWorkload));
    }
}

void MultithreadedWorkload::resetBlockingWorkflowTo(int threadCount, double ratioOfTimeToBlock, int iterationsBeforeSleep) {
    assert(threadCount >= 0);

    // Clean up all blocking workloads.
    _removeExtraWorkloadsByType(0, true);
    std::cerr << "All blocking workloads removed, " << _workloads.size() << " non blocking remain" << std::endl;

    for (int i = 0; i < threadCount; ++i) {
        auto workload = _createCallback();
        assert(workload);
        auto threadWorkload = std::make_unique<ThreadPartiallyBlockedWorkload>(
            std::move(workload), ratioOfTimeToBlock, iterationsBeforeSleep);
        threadWorkload->start();
        _workloads.push_back(std::move(threadWorkload));
    }
    std::cerr << _workloads.size() << " total workload size" << std::endl;
}

void MultithreadedWorkload::resetStats() {
    for (const auto& w : _workloads) {
        w->resetStats();
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
}

MultithreadedWorkload::ThreadWorkload::~ThreadWorkload() {
    terminate();
}

void MultithreadedWorkload::ThreadWorkload::start() {
    _thread = std::make_unique<std::thread>([this] {
        Stats localStats;
        auto start = std::chrono::high_resolution_clock::now();

        while (!_terminate.load(std::memory_order_relaxed)) {
            _workload->unitOfWork();
            if (++localStats.iterations < 100) {
                continue;
            }

            auto now = std::chrono::high_resolution_clock::now();
            localStats.duration =
                std::chrono::duration_cast<std::chrono::microseconds>(now - start);
            start = now;
            std::lock_guard<std::mutex> guard(_mutex);
            _stats.append(localStats);
            localStats = Stats();  // Reset for new cycle.
        }
    });
}

void MultithreadedWorkload::ThreadWorkload::terminate() {
    _terminate = true;
    if (_thread) {
        _thread->join();
        _thread.reset();
    }
}


MultithreadedWorkload::ThreadPartiallyBlockedWorkload::ThreadPartiallyBlockedWorkload(
    std::unique_ptr<Workload> workload, double ratioOfTimeToBlock, int iterationsBeforeSleep)
    : ThreadWorkload(std::move(workload)),
      _ratioOfTimeToBlock(ratioOfTimeToBlock),
      _iterationsBeforeSleep(iterationsBeforeSleep) {
        assert(_iterationsBeforeSleep >= 1);
}

void MultithreadedWorkload::ThreadPartiallyBlockedWorkload::start() {
    _thread = std::make_unique<std::thread>([this] {
        Stats localStats;
        auto iterationStart = std::chrono::high_resolution_clock::now();

        while (!_terminate.load(std::memory_order_relaxed)) {
            _workload->unitOfWork();
            if (++localStats.iterations < _iterationsBeforeSleep) {
                continue;
            }

            auto now = std::chrono::high_resolution_clock::now();
            auto duration =
                std::chrono::duration_cast<std::chrono::microseconds>(now - iterationStart);

            // Sleep.
            static thread_local std::mt19937 gen;
            auto timeToSleep = (now - iterationStart) * _ratioOfTimeToBlock;
            std::uniform_int_distribution<std::mt19937::result_type> distrib(
                0, std::chrono::duration_cast<std::chrono::microseconds>(timeToSleep / 40).count());
            std::this_thread::sleep_for(timeToSleep + std::chrono::microseconds(distrib(gen)));

            // Adjust stats
            now = std::chrono::high_resolution_clock::now();
            std::lock_guard<std::mutex> guard(_mutex);
            localStats.duration = std::chrono::duration_cast<std::chrono::microseconds>(
                now - iterationStart);
            _stats.append(localStats);
            localStats = Stats();  // Reset for new cycle.
            iterationStart = now;
        }
    });
}

}  // namespace testing
}  // namespace blocking_to_async 
