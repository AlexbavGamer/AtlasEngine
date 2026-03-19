#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>

namespace Atlas {

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency()) {
        m_Working = true;
        for (size_t i = 0; i < numThreads; ++i) {
            m_Threads.emplace_back([this] {
                while (m_Working) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(m_Mutex);
                        m_Condition.wait(lock, [this] {
                            return !m_Working || !m_Tasks.empty();
                        });
                        
                        if (!m_Working && m_Tasks.empty()) {
                            return;
                        }
                        
                        if (!m_Tasks.empty()) {
                            task = std::move(m_Tasks.front());
                            m_Tasks.pop();
                        }
                    }
                    
                    if (task) {
                        task();
                    }
                }
            });
        }
    }
    
    ~ThreadPool() {
        m_Working = false;
        m_Condition.notify_all();
        for (auto& thread : m_Threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
    
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> result = task->get_future();
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Tasks.emplace([task]() { (*task)(); });
        }
        m_Condition.notify_one();
        
        return result;
    }
    
    size_t numThreads() const { return m_Threads.size(); }
    
    size_t pendingTasks() {
        std::unique_lock<std::mutex> lock(m_Mutex);
        return m_Tasks.size();
    }

private:
    std::vector<std::thread> m_Threads;
    std::queue<std::function<void()>> m_Tasks;
    std::mutex m_Mutex;
    std::condition_variable m_Condition;
    bool m_Working;
};

}
