#pragma once

#include <cstddef>
#include <memory>
#include <cstring>

namespace Atlas {

class Allocator {
public:
    Allocator() = default;
    virtual ~Allocator() = default;

    virtual void* allocate(size_t size, size_t alignment = 8) = 0;
    virtual void free(void* ptr) = 0;
    virtual size_t getUsedMemory() const = 0;
    virtual size_t getTotalAllocations() const = 0;
};

class MallocAllocator : public Allocator {
public:
    void* allocate(size_t size, size_t alignment = 8) override {
        (void)alignment;
        return std::malloc(size);
    }

    void free(void* ptr) override {
        std::free(ptr);
    }

    size_t getUsedMemory() const override { return 0; }
    size_t getTotalAllocations() const override { return 0; }
};

template<typename T>
class Deleter {
public:
    Deleter(Allocator* allocator = nullptr) : m_Allocator(allocator) {}

    void operator()(T* ptr) {
        if (ptr) {
            ptr->~T();
            if (m_Allocator) {
                m_Allocator->free(ptr);
            } else {
                std::free(ptr);
            }
        }
    }

private:
    Allocator* m_Allocator = nullptr;
};

template<typename T>
using UniquePtr = std::unique_ptr<T, Deleter<T>>;

template<typename T, typename... Args>
UniquePtr<T> makeUnique(Allocator* allocator, Args&&... args) {
    void* ptr = allocator ? allocator->allocate(sizeof(T), alignof(T)) : std::malloc(sizeof(T));
    if (!ptr) throw std::bad_alloc();
    
    T* object = new(ptr) T(std::forward<Args>(args)...);
    return UniquePtr<T>(object, Deleter<T>(allocator));
}

template<typename T>
UniquePtr<T> makeUnique() {
    return makeUnique<T>(nullptr);
}

}
