#include <condition_variable>
#include <functional>
#include <queue>
#include <mutex>
#include <thread>

namespace blocking_to_async {
namespace testing {

class ThreadPool {
public:
    void start(
        int concurrency,
        std::function<void()> callbackWhenHasCapacity);
    void queueJob(const std::function<void()>& job);
    void stop();
    int queueSize() const;

private:
    void _threadLoop(int threadId);

    std::function<void()> _callbackWhenHasCapacity;
    bool _shouldTerminate = false;           // Tells threads to stop looking for jobs
    mutable std::mutex _queueMutex;
    std::condition_variable _mutexCondition; // Allows threads to wait on new jobs or termination 
    std::vector<std::thread> _threads;
    std::queue<std::function<void()>> _jobs;
};

}  // namespace testing
}  // namespace blocking_to_async 
