/**
 * @author Andrew Shuvalov
 * @brief Benchmark the overhead of context switches.
 * @date 2022-07-18
 */

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <thread>

#include "benchmarks/thread_pool.h"

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

    size_t sharedDataSize = kExpectedL2CacheSize * 1000 * 2.5;
    size_t memoryWorkSizePerIteration = kExpectedL1CacheSize / 4;

    // This is filled up by calibration results.
    OptimalConcurrency optimalConcurrency;
};

struct Stats {
    std::chrono::microseconds duration{ 0 };
    uint64_t iterations = 0;
    int threadMigrations = 0;
    int minflt = 0;
    int majflt = 0;

    double qps() const;
    double migrationsQps() const;
    double minfltQps() const;

    // Increase iterations for the same interval.
    void appendConcurrent(const Stats& other);

    void append(const Stats& other);

    Stats diff(const Stats& other) const;

    static std::tuple<int, int> getPageFaults();
};

inline std::ostream& operator<<(std::ostream& os, const Stats& s) {
    os << "duration: " << s.duration.count() << " us iterations: " << s.iterations;
    if (s.iterations > 0) { os << " QPS: " << s.qps(); }
    if (s.threadMigrations > 0) { os << " Migrations: " << s.migrationsQps() << " /s"; }
    if (s.minflt > 0) { os << " minflt: " << s.minflt; }
    if (s.majflt > 0) { os << " minflt: " << s.majflt; }
    return os;
}

class Workload {
public:
    virtual ~Workload() = default;

    virtual void init(const Config& config) = 0;

    // Returns the count of Core ID switches (thread migrations) for this thread while
    // doing the unit of work.
    virtual int unitOfWork() = 0;
};

class MultithreadedWorkload {
public:
    MultithreadedWorkload(std::function<std::unique_ptr<Workload>()> createCallback);

    int threadCount() const {
        return _workloads.size();
    }

    void scaleNonBlockingWorkloadTo(int newThreadCount);

    void resetBlockingWorkflowTo(int threadCount, double ratioOfTimeToBlock, int iterationsBeforeSleep);

    void startPooledWorkload(int threadCount, double ratioOfTimeToBlock, int iterationsBeforeSleep);

    void stopPooledWorkload();

    // Reset at the beginning of an experiment.
    void resetStats();

    Stats getStats() const;

    std::string status() const;

private:
    // Simple continuous thread workload. The passed in `Workload` is not thread aware and
    // only does `unitOfWork()`.
    class ThreadWorkload {
    public:
        enum class WorkloadType { kNonBlocking, kBlocking, kBlockingPooled };

        explicit ThreadWorkload(std::unique_ptr<Workload> workload);
        ThreadWorkload(ThreadWorkload& other) = delete;

        virtual ~ThreadWorkload();

        virtual WorkloadType workloadType() const {
            return WorkloadType::kNonBlocking;
        }

        virtual void start();

        virtual void resetStats() {
            std::lock_guard<std::mutex> guard(_mutex);
            _stats = Stats();
        }

        Stats getStats() const {
            std::lock_guard<std::mutex> guard(_mutex);
            return _stats;
        }

        void terminate();

        virtual std::string status() const { return ""; }

        // Get current CPU core ID.
        static unsigned getCoreId();

    protected:
        // Sleep that supports being interrupted.
        void _sleep(std::chrono::microseconds sleepFor);

        std::unique_ptr<std::thread> _thread;
        std::unique_ptr<Workload> _workload;
        std::atomic<bool> _terminate;

        mutable std::mutex _mutex;
        Stats _stats;

        std::vector<std::unique_ptr<std::condition_variable>> _perCoreSleepCv;
        int _currentSleepDeprivedCore = 0;
        std::chrono::time_point<std::chrono::high_resolution_clock> _lastSleepChange =
            std::chrono::high_resolution_clock::now();
    };

    // This variant is capable to block a thread before units of work.
    class ThreadPartiallyBlockedWorkload : public ThreadWorkload {
    public:
        ThreadPartiallyBlockedWorkload(std::unique_ptr<Workload> workload, 
                                       double ratioOfTimeToBlock,
                                       int iterationsBeforeSleep);
        ~ThreadPartiallyBlockedWorkload() override = default;

        WorkloadType workloadType() const override {
            return ThreadWorkload::WorkloadType::kBlocking;
        }

        void start() override;

    private:
        const double _ratioOfTimeToBlock;
        const int _iterationsBeforeSleep;
    };

    // This variant performs identical amount of work as one above but using thread pools -
    // one for non-blocking workloads and another for blocked parts.
    class ThreadPoolWorkload : public ThreadWorkload {
    public:
        ThreadPoolWorkload(std::unique_ptr<Workload> workload, 
                           double ratioOfTimeToBlock,
                           int iterationsBeforeSleep,
                           int threadCount);
        ~ThreadPoolWorkload() override;

        WorkloadType workloadType() const override {
            return ThreadWorkload::WorkloadType::kBlockingPooled;
        }

        void start() override;

        void resetStats() override {
            std::lock_guard<std::mutex> guard(_mutex);
            _stats = Stats();
            _measurementsStart = std::chrono::high_resolution_clock::now();
        }

        std::string status() const override;

    private:
        std::function<void()> unblockedWorkloadThreadPoolJob();

        std::chrono::time_point<std::chrono::high_resolution_clock> _measurementsStart =
            std::chrono::high_resolution_clock::now();
        const double _ratioOfTimeToBlock;
        const int _iterationsBeforeSleep;
        const int _threadCount;

        ThreadPool _unblockedWorkloadThreadPool;
        ThreadPool _blockingCallsThreadPool;
    };

    // Returns total count of type remaining after deletion.
    int _removeExtraWorkloadsByType(int newThreadCount, ThreadWorkload::WorkloadType workloadType);

    const std::function<std::unique_ptr<Workload>()> _createCallback;

    std::vector<std::unique_ptr<ThreadWorkload>> _workloads;
};

}  // namespace testing
}  // namespace blocking_to_async 
