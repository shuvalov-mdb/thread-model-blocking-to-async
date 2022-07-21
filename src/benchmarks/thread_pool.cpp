#include "benchmarks/thread_pool.h"
#include <iostream>
#include <ostream>

namespace blocking_to_async {
namespace testing {

void ThreadPool::start(int concurrency, std::function<void()> callbackWhenHasCapacity) {
    _callbackWhenHasCapacity = std::move(callbackWhenHasCapacity);
    _threads.resize(concurrency);
    for (uint32_t i = 0; i < concurrency; i++) {
        _threads.at(i) = std::thread([this, i] { _threadLoop(i); });
        if (_callbackWhenHasCapacity) {
            _callbackWhenHasCapacity();  // Call for jobs. 
        }
    }
}

void ThreadPool::queueJob(const std::function<void()>& job) {
    {
        std::unique_lock<std::mutex> lock(_queueMutex);
        _jobs.push(job);
        if (_jobs.size() > 10) {
            std::cerr << "Warning " << _jobs.size() << " jobs in queue";
        }
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

int ThreadPool::queueSize() const {
    std::unique_lock<std::mutex> lock(_queueMutex);
    return _jobs.size();
}

void ThreadPool::_threadLoop(int threadId) {
    int count = 0;
    while (true) {
        std::function<void()> job;
        bool shouldCallCapacityCallback = false;
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            if (_jobs.empty()) {
                _mutexCondition.wait(lock, [this] {
                    return !_jobs.empty() || _shouldTerminate;
                });
            }
            if (_shouldTerminate) {
                // std::cerr << "Thread " << threadId << " executed " << count << " jobs" << std::endl;
                return;
            }
            job = _jobs.front();
            _jobs.pop();
            if (_jobs.empty()) {
                shouldCallCapacityCallback = true;
            }
        }
        while (shouldCallCapacityCallback && _callbackWhenHasCapacity) {
            _callbackWhenHasCapacity();
            std::unique_lock<std::mutex> lock(_queueMutex);
            shouldCallCapacityCallback = _jobs.empty();
        }
        job();
        ++count;
    }
}


}  // namespace testing
}  // namespace blocking_to_async 

