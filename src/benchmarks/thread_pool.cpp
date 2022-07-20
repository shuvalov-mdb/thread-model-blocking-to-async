#include "benchmarks/thread_pool.h"

namespace blocking_to_async {
namespace testing {

void ThreadPool::start(int concurrency) {
    _threads.resize(concurrency);
    for (uint32_t i = 0; i < concurrency; i++) {
        _threads.at(i) = std::thread([this] { _threadLoop(); });
    }    
}

void ThreadPool::queueJob(const std::function<void()>& job) {
    {
        std::unique_lock<std::mutex> lock(_queueMutex);
        _jobs.push(job);
    }
    _mutexCondition.notify_one();
}

void ThreadPool::stop() {
    {
        std::unique_lock<std::mutex> lock(_queueMutex);
        _shouldTerminate = true;
    }
    _mutexCondition.notify_all();
    for (std::thread& active_thread : _threads) {
        active_thread.join();
    }
    _threads.clear();
}

bool ThreadPool::busy() const {
    bool poolbusy;
    {
        std::unique_lock<std::mutex> lock(_queueMutex);
        poolbusy = _jobs.empty();
    }
    return poolbusy;
}

void ThreadPool::_threadLoop() {
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            _mutexCondition.wait(lock, [this] {
                return !_jobs.empty() || _shouldTerminate;
            });
            if (_shouldTerminate) {
                return;
            }
            job = _jobs.front();
            _jobs.pop();
        }
        job();
    }    
}


}  // namespace testing
}  // namespace blocking_to_async 

