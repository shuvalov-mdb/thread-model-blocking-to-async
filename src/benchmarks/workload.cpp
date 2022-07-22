/**
 * @author Andrew Shuvalov
 * @brief Benchmark the overhead of context switches.
 * @date 2022-07-18
 */

#include "benchmarks/workload.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string.h>
#include <x86intrin.h>
#include <unistd.h>

namespace blocking_to_async {
namespace testing {

double Stats::qps() const {
    return iterations * 1000.0 * 1000 / duration.count();
}

double Stats::migrationsQps() const {
    return threadMigrations * 1000.0 * 1000 / duration.count();
}

double Stats::minfltQps() const {
    return minflt * 1000.0 * 1000 / duration.count();
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
    threadMigrations += other.threadMigrations;
}

void Stats::append(const Stats& other) {
    duration += other.duration;
    iterations += other.iterations;
    threadMigrations += other.threadMigrations;
}

Stats Stats::diff(const Stats& other) const {
    Stats result;
    result.duration = duration - other.duration;
    result.iterations = iterations - other.iterations;
    result.threadMigrations = threadMigrations - other.threadMigrations;
    result.minflt = minflt - other.minflt;
    result.majflt = majflt - other.majflt;
    return result;
}

std::tuple<int, int> Stats::getPageFaults() {
    pid_t id = getpid();
    std::string path = "/proc/" + std::to_string(id) + "/stat";
    std::ifstream infile(path);
    assert(infile.is_open());
    std::string line;
    std::getline(infile, line);
    std::istringstream iss(line);

    long int pid, ppid, pgrp, session, tty_nr, tpgid, flags, minflt, cminflt, majflt;
    std::string comm, state;
    iss >> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags
        >> minflt >> cminflt >> majflt;
    assert(pid == id);
    return { minflt, majflt };
}

MultithreadedWorkload::MultithreadedWorkload(
    std::function<std::unique_ptr<Workload>()> createCallback)
    : _createCallback(createCallback) {
}

int MultithreadedWorkload::_removeExtraWorkloadsByType(
    int newThreadCount, ThreadWorkload::WorkloadType workloadType) {
    int countFound = 0;
    for (auto it = _workloads.begin(); it != _workloads.end();) {
        if ((*it)->workloadType() == workloadType) { 
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

    int remaining = _removeExtraWorkloadsByType(newThreadCount, ThreadWorkload::WorkloadType::kNonBlocking);

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
    _removeExtraWorkloadsByType(0, ThreadWorkload::WorkloadType::kBlocking);
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

void MultithreadedWorkload::startPooledWorkload(
    int threadCount, double ratioOfTimeToBlock, int iterationsBeforeSleep) {
    auto workload = _createCallback();
    assert(workload);
    auto threadWorkload = std::make_unique<ThreadPoolWorkload>(
        std::move(workload), ratioOfTimeToBlock, iterationsBeforeSleep, threadCount);
    threadWorkload->start();
    _workloads.push_back(std::move(threadWorkload));
    std::cerr << "Workloads size " << _workloads.size() << std::endl;
}

void MultithreadedWorkload::stopPooledWorkload() {
    _removeExtraWorkloadsByType(0, ThreadWorkload::WorkloadType::kBlockingPooled);
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
    std::tie(result.minflt, result.majflt) = Stats::getPageFaults();
    return result;
}

std::string MultithreadedWorkload::status() const {
    if (!_workloads.empty()) {
        return _workloads[0]->status();
    }
    return "";
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

unsigned MultithreadedWorkload::ThreadWorkload::getCoreId() {
    unsigned id;
    __rdtscp(&id);
    return id;
}

void MultithreadedWorkload::ThreadWorkload::_sleep(std::chrono::microseconds sleepFor) {
    const auto startCoreId = getCoreId();
    std::unique_lock<std::mutex> guard(_mutex);
    while (startCoreId + 1 > _perCoreSleepCv.size()) {
        _perCoreSleepCv.emplace_back(new std::condition_variable);
    }

    if (std::chrono::high_resolution_clock::now() > _lastSleepChange + std::chrono::milliseconds(10)) {
        static thread_local std::mt19937 gen;
        std::uniform_int_distribution<std::mt19937::result_type> distrib(
            0, _perCoreSleepCv.size() - 1);
        _currentSleepDeprivedCore = distrib(gen);
        _lastSleepChange = std::chrono::high_resolution_clock::now();
        // Wake up all threads sleeping on this core runqueue.
        _perCoreSleepCv[_currentSleepDeprivedCore]->notify_all();
    }

    _perCoreSleepCv[startCoreId]->wait_for(guard, sleepFor, [this, startCoreId] {
        return startCoreId == _currentSleepDeprivedCore;
    });
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
        int threadMigrations = 0;

        while (!_terminate.load(std::memory_order_relaxed)) {
            threadMigrations += _workload->unitOfWork();
            if (++localStats.iterations < _iterationsBeforeSleep) {
                continue;
            }

            auto previousCoreId = getCoreId();
            auto now = std::chrono::high_resolution_clock::now();
            auto duration =
                std::chrono::duration_cast<std::chrono::microseconds>(now - iterationStart);

            // Sleep.
            static thread_local std::mt19937 gen;
            auto timeActive = now - iterationStart;
            auto timeToSleep = 1 / (1 - _ratioOfTimeToBlock) * timeActive - timeActive;
            std::uniform_int_distribution<std::mt19937::result_type> distrib(
                0, std::chrono::duration_cast<std::chrono::microseconds>(timeToSleep / 40).count());
            _sleep(std::chrono::duration_cast<std::chrono::microseconds>(timeToSleep) + 
                   std::chrono::microseconds(distrib(gen)));

            // Adjust stats
            now = std::chrono::high_resolution_clock::now();
            std::lock_guard<std::mutex> guard(_mutex);
            localStats.duration = std::chrono::duration_cast<std::chrono::microseconds>(
                now - iterationStart);
            _stats.append(localStats);
            _stats.threadMigrations = threadMigrations;
            if (previousCoreId != getCoreId()) {
                ++_stats.threadMigrations;
            }
            threadMigrations = 0;
            localStats = Stats();  // Reset for new cycle.
            iterationStart = now;
        }
    });
}

MultithreadedWorkload::ThreadPoolWorkload::ThreadPoolWorkload(
    std::unique_ptr<Workload> workload, double ratioOfTimeToBlock, 
    int iterationsBeforeSleep, int threadCount)
    : ThreadWorkload(std::move(workload)),
      _ratioOfTimeToBlock(ratioOfTimeToBlock),
      _iterationsBeforeSleep(iterationsBeforeSleep),
      _threadCount(threadCount) {
        assert(_iterationsBeforeSleep >= 1);
        assert(threadCount >= 1);
}

MultithreadedWorkload::ThreadPoolWorkload::~ThreadPoolWorkload() {
    _unblockedWorkloadThreadPool.stop();
    _blockingCallsThreadPool.stop();
}

void MultithreadedWorkload::ThreadPoolWorkload::start() {
    // Unlike workloads below the pooled workload has only one instance.
    _unblockedWorkloadThreadPool.start(_threadCount);
    _blockingCallsThreadPool.start(std::min(_threadCount * 20, 800));
    // Let threads start.
    while (!_unblockedWorkloadThreadPool.isWarm() || !_blockingCallsThreadPool.isWarm()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    for (int i = 0; i <= _threadCount; ++i) {
        _unblockedWorkloadThreadPool.queueJob(unblockedWorkloadThreadPoolJob());
    }
}

std::function<void()> MultithreadedWorkload::ThreadPoolWorkload::unblockedWorkloadThreadPoolJob() {
    return [this] {
        Stats localStats;
        auto iterationStart = std::chrono::high_resolution_clock::now();
        int threadMigrations = 0;

        if (_terminate.load(std::memory_order_relaxed)) {
            _unblockedWorkloadThreadPool.stop();
            return;
        }

        while (true) {
            threadMigrations += _workload->unitOfWork();
            if (++localStats.iterations >= _iterationsBeforeSleep) {
                break;
            }
        }

        auto now = std::chrono::high_resolution_clock::now();
        auto duration =
            std::chrono::duration_cast<std::chrono::microseconds>(now - iterationStart);

        // Calculate sleep time.
        static thread_local std::mt19937 gen;
        auto timeActive = now - iterationStart;
        auto timeToSleep = 1 / (1 - _ratioOfTimeToBlock) * timeActive - timeActive;
        std::uniform_int_distribution<std::mt19937::result_type> distrib(
            0, std::chrono::duration_cast<std::chrono::microseconds>(timeToSleep / 40).count());
        timeToSleep += std::chrono::microseconds(distrib(gen));

        // Adjust stats
        {
            std::lock_guard<std::mutex> guard(_mutex);
            _stats.duration = std::chrono::duration_cast<std::chrono::microseconds>(now - _measurementsStart);
            _stats.iterations += localStats.iterations;
            _stats.threadMigrations += threadMigrations;
        }

        _blockingCallsThreadPool.queueJob(
            [this, timeToSleep, localStats, threadMigrations] {
            _sleep(std::chrono::duration_cast<std::chrono::microseconds>(timeToSleep));

            if (!_terminate.load(std::memory_order_relaxed)) {
                auto workloadQueueSize = _unblockedWorkloadThreadPool.queueSize();
                if ((workloadQueueSize < 5 ||
                     _unblockedWorkloadThreadPool.spareCapacity() >= workloadQueueSize) &&
                    _blockingCallsThreadPool.spareCapacity() > 10) {
                    _unblockedWorkloadThreadPool.queueJob(unblockedWorkloadThreadPoolJob());
                    _unblockedWorkloadThreadPool.queueJob(unblockedWorkloadThreadPoolJob());
                }
            }
        });
    };
}

std::string MultithreadedWorkload::ThreadPoolWorkload::status() const {
    return "workloads running: " + std::to_string(_unblockedWorkloadThreadPool.currentlyRunning()) +
        " blocking running: " + std::to_string(_blockingCallsThreadPool.currentlyRunning());
}

}  // namespace testing
}  // namespace blocking_to_async 
