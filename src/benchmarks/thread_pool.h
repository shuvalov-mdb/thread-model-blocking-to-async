#include <atomic>
#include <condition_variable>
#include <functional>
#include <queue>
#include <mutex>
#include <thread>

namespace blocking_to_async {
namespace testing {

class ThreadPool {
public:
    void start(int concurrency);
    bool isWarm() const;
    void queueJob(const std::function<void()>& job);
    void stop();
    int queueSize() const;
    int currentlyRunning() const;
    int spareCapacity() const;

private:
    void _threadLoop(int threadId);

    bool _shouldTerminate = false;           // Tells threads to stop looking for jobs
    mutable std::mutex _queueMutex;
    int _capacity;
    std::condition_variable _mutexCondition; // Allows threads to wait on new jobs or termination 
    std::vector<std::thread> _threads;
    std::queue<std::function<void()>> _jobs;
    std::atomic<int> _currentlyRunning{0};
    std::atomic<int> _startedThreads{0};
};

}  // namespace testing
}  // namespace blocking_to_async 
