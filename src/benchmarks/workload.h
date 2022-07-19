/**
 * @author Andrew Shuvalov
 * @brief Benchmark the overhead of context switches.
 * @date 2022-07-18
 */

#include <atomic>
#include <cassert>
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <thread>

namespace blocking_to_async {
namespace testing {

struct OptimalConcurrency {
    int threadCount = 0;
    double qps = 0.0;
};

struct Config {
    // Cache size for 8275CL per core.
    static constexpr size_t kExpectedL1CacheSize = 1024 * 32;
    static constexpr size_t kExpectedL2CacheSize = 1024 * 1024;

    size_t dataSizePerThread = kExpectedL2CacheSize * 2;
    size_t memoryWorkSizePerIteration = kExpectedL1CacheSize;

    // This is filled up by calibration results.
    OptimalConcurrency optimalConcurrency;
};

struct Stats {
    std::chrono::microseconds duration{ 0 };
    uint64_t iterations = 0;

    // Increase iterations for the same interval.
    void appendConcurrent(const Stats& other);

    void appendDecaying(const Stats& other);
};

inline std::ostream& operator<<(std::ostream& os, const Stats& s) {
    os << "duration: " << s.duration.count() << " us iterations: " << s.iterations;
    if (s.iterations > 0) { os << " QPS: " << (s.iterations * 1000. * 1000 / s.duration.count()); }
    return os;
}

class Workload {
public:
    virtual ~Workload() = default;

    virtual void init(const Config& config) = 0;

    virtual void unitOfWork() = 0;
};

class MultithreadedWorkload {
public:
    MultithreadedWorkload(std::function<std::unique_ptr<Workload>()> createCallback);

    int threadCount() const {
        return _workloads.size();
    }

    void scaleNonBlockingWorkloadTo(int newThreadCount);

    Stats getStats() const;

private:
    // Simple continuous thread workload. The passed in `Workload` is not thread aware and
    // only does `unitOfWork()`.
    class ThreadWorkload {
    public:
        explicit ThreadWorkload(std::unique_ptr<Workload> workload);
        ThreadWorkload(ThreadWorkload& other) = delete;

        ~ThreadWorkload();

        Stats getStats() const {
            std::lock_guard<std::mutex> guard(_mutex);
            return _stats;
        }

        void terminate();

    protected:
        std::unique_ptr<std::thread> _thread;
        std::unique_ptr<Workload> _workload;
        std::atomic<bool> _terminate;

        mutable std::mutex _mutex;
        Stats _stats;
    };

    // This variant is capable to block a thread before units of work.
    class ThreadPartiallyBlockedWorkload : public ThreadWorkload {
    public:
        ThreadPartiallyBlockedWorkload(std::unique_ptr<Workload> workload);
    };

    const std::function<std::unique_ptr<Workload>()> _createCallback;

    std::vector<std::unique_ptr<ThreadWorkload>> _workloads;
};

}  // namespace testing
}  // namespace blocking_to_async 
