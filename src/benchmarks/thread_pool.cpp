#include "benchmarks/thread_pool.h"
#include <iostream>
#include <ostream>

namespace blocking_to_async {
namespace testing {

void ThreadPool::start(int concurrency) {
    _capacity = concurrency;
    _threads.resize(concurrency);
    for (uint32_t i = 0; i < concurrency; i++) {
        _threads.at(i) = std::thread([this, i] { _threadLoop(i); });
    }
}

bool ThreadPool::isWarm() const {
    return _startedThreads == _capacity;
}

void ThreadPool::queueJob(const std::function<void()>& job) {
    bool shouldNotify = false;
    {
        std::unique_lock<std::mutex> lock(_queueMutex);
        shouldNotify = _jobs.size() > 1 || _currentlyRunning.load() < std::max(8, _capacity / 4);
        //shouldNotify = true;
        _jobs.push(job);
        if (_jobs.size() > 20 || (_jobs.size() > 5 && _currentlyRunning < _capacity / 2)) {
            // std::cerr << "Warning " << _jobs.size() << " jobs in queue, running " << _currentlyRunning << std::endl;
        }
    }
    if (shouldNotify) {
        _mutexCondition.notify_one();
    }
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

int ThreadPool::currentlyRunning() const {
    return _currentlyRunning;
}

int ThreadPool::spareCapacity() const {
    return _capacity - _currentlyRunning;
}



void ThreadPool::_threadLoop(int threadId) {
    ++_startedThreads;
    int count = 0;
    while (true) {
        std::function<void()> job;
        bool shouldNotify = false;
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
            if (!_jobs.empty()) {
                shouldNotify = true;
            }
        }
        if (shouldNotify) {
            _mutexCondition.notify_one();
        }
        ++_currentlyRunning;
        job();
        --_currentlyRunning;
        ++count;
    }
}


}  // namespace testing
}  // namespace blocking_to_async 

