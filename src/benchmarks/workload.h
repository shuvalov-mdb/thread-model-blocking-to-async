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

struct Config {
    // Cache size for 8275CL per core.
    static constexpr size_t kExpectedL1CacheSize = 1024 * 32;
    static constexpr size_t kExpectedL2CacheSize = 1024 * 1024;

    size_t dataSizePerThread = kExpectedL2CacheSize * 2;
    size_t memoryWorkSizePerIteration = kExpectedL1CacheSize;

    std::chrono::milliseconds stableResultsDuringCalibration{ 1000 };
};

struct Stats {
    std::chrono::microseconds duration{ 0 };
    uint64_t iterations = 0;

    // Increase iterations for the same interval.
    void appendConcurrent(const Stats& other) {
        if (other.duration == std::chrono::microseconds{ 0 }) {
            return;
        }
        if (duration == std::chrono::microseconds{ 0 }) {
            *this = other;
            return;  // First time append into empty.
        }

        iterations += other.iterations * duration / other.duration;
    }
};

inline std::ostream& operator<<(std::ostream& os, const Stats& s) {
    os << "duration: " << s.duration.count() << " us iterations: " << s.iterations;
    if (s.iterations > 0) { os << " QPS: " << (s.iterations * 1000 * 1000 / s.duration.count()); }
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

    void scaleTo(int newThreadCount);

    Stats getStats() const;

private:
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

    private:
        std::unique_ptr<std::thread> _thread;
        std::unique_ptr<Workload> _workload;
        std::atomic<bool> _terminate;

        mutable std::mutex _mutex;
        Stats _stats;
    };

    const std::function<std::unique_ptr<Workload>()> _createCallback;

    std::vector<std::unique_ptr<ThreadWorkload>> _workloads;
};

}  // namespace testing
}  // namespace blocking_to_async 
