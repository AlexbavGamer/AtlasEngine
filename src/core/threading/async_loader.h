#pragma once

#include "thread_pool.h"
#include <unordered_map>
#include <functional>
#include <atomic>
#include <memory>

namespace Atlas {

struct LoadingTask {
    std::string path;
    std::atomic<bool> completed{false};
    std::atomic<bool> failed{false};
    std::string errorMessage;
    std::function<void()> callback;
};

class AsyncLoader {
public:
    static AsyncLoader& getInstance() {
        static AsyncLoader instance;
        return instance;
    }
    
    void init() {
        if (!m_ThreadPool) {
            m_ThreadPool = std::make_unique<ThreadPool>(2);
        }
    }
    
    void shutdown() {
        m_ThreadPool.reset();
    }
    
    template<typename T>
    struct LoadResult {
        std::shared_ptr<T> data;
        bool success = false;
        std::string error;
    };
    
    template<typename T, typename LoadFunc>
    void loadModelAsync(const std::string& path, LoadFunc&& loadFunc, std::function<void(LoadResult<T>)> onComplete) {
        auto task = std::make_shared<LoadingTask>();
        task->path = path;
        
        m_ThreadPool->enqueue([this, task, loadFunc = std::forward<LoadFunc>(loadFunc), onComplete]() {
            std::cout << "  [ASYNC] Task started" << std::endl; std::cout.flush();
            LoadResult<T> result;
            try {
                std::cout << "  [ASYNC] Calling loadFunc..." << std::endl; std::cout.flush();
                try {
                    std::cout << "  [ASYNC] calling loadFunc..." << std::endl; std::cout.flush();
                    result.data = loadFunc();
                    std::cout << "  [ASYNC] loadFunc returned, data=" << (result.data ? "valid" : "null") << std::endl; std::cout.flush();
                } catch (...) {
                    // rethrow to be caught below
                    throw;
                }
                std::cout << "  [ASYNC] loadFunc returned, success=" << result.success << std::endl; std::cout.flush();
                result.success = true;
                std::cout << "  [ASYNC] Load complete, success=" << result.success << std::endl; std::cout.flush();
            } catch (const std::exception& e) {
                result.success = false;
                result.error = e.what();
                task->failed = true;
                task->errorMessage = e.what();
                std::cout << "  [ASYNC] Load failed: " << e.what() << std::endl; std::cout.flush();
            }
            task->completed = true;
            
            std::cout << "  [ASYNC] Calling callback..." << std::endl; std::cout.flush();
            if (onComplete) {
                try {
                    onComplete(result);
                    std::cout << "  [ASYNC] Callback complete" << std::endl; std::cout.flush();
                } catch (const std::exception& e) {
                    std::cout << "  [ASYNC] Callback exception: " << e.what() << std::endl; std::cout.flush();
                }
            }
        });
        
        std::lock_guard<std::mutex> lock(m_TasksMutex);
        m_Tasks[path] = task;
    }
    
    bool isLoading(const std::string& path) const {
        std::lock_guard<std::mutex> lock(m_TasksMutex);
        auto it = m_Tasks.find(path);
        if (it != m_Tasks.end()) {
            return !it->second->completed;
        }
        return false;
    }
    
    bool hasCompleted(const std::string& path) const {
        std::lock_guard<std::mutex> lock(m_TasksMutex);
        auto it = m_Tasks.find(path);
        if (it != m_Tasks.end()) {
            return it->second->completed && !it->second->failed;
        }
        return false;
    }
    
    void clearCompleted() {
        std::lock_guard<std::mutex> lock(m_TasksMutex);
        for (auto it = m_Tasks.begin(); it != m_Tasks.end(); ) {
            if (it->second->completed) {
                it = m_Tasks.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    size_t getPendingCount() const {
        std::lock_guard<std::mutex> lock(m_TasksMutex);
        size_t count = 0;
        for (const auto& [path, task] : m_Tasks) {
            if (!task->completed) {
                count++;
            }
        }
        return count;
    }

private:
    AsyncLoader() = default;
    
    std::unique_ptr<ThreadPool> m_ThreadPool;
    std::unordered_map<std::string, std::shared_ptr<LoadingTask>> m_Tasks;
    mutable std::mutex m_TasksMutex;
};

}
