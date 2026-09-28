// PRISM ENGINE — core/pool.h : monotonic arena + free-list object pool (mobile allocators).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#include "../core/types.h"

namespace prism {

/// Bump allocator for per-frame scratch data. Never frees individually.
class LinearAllocator {
public:
    explicit LinearAllocator(std::size_t bytes = 1u << 20) { reset(bytes); }
    void reset(std::size_t bytes) {
        buffer_.resize(bytes);
        offset_ = 0;
    }
    void* allocate(std::size_t size, std::size_t align = 16) {
        std::uintptr_t base = reinterpret_cast<std::uintptr_t>(buffer_.data());
        std::uintptr_t cur  = base + offset_;
        std::uintptr_t aligned = (cur + (align - 1)) & ~(align - 1);
        std::size_t new_offset = static_cast<std::size_t>(aligned - base) + size;
        if (new_offset > buffer_.size()) { ++overflows_; return nullptr; }
        offset_ = new_offset;
        peak_ = offset_ > peak_ ? offset_ : peak_;
        return reinterpret_cast<void*>(aligned);
    }
    void rewind() { offset_ = 0; }
    [[nodiscard]] std::size_t used()     const { return offset_; }
    [[nodiscard]] std::size_t peak()     const { return peak_; }
    [[nodiscard]] std::size_t capacity() const { return buffer_.size(); }
    [[nodiscard]] u64 overflows()        const { return overflows_; }
private:
    std::vector<std::byte> buffer_;
    std::size_t offset_ = 0, peak_ = 0;
    u64 overflows_ = 0;
};

/// Free-list pool. O(1) acquire/release, zero allocations after warm-up.
template <typename T, std::size_t BlockSize = 512>
class ObjectPool {
public:
    ObjectPool() { static_assert(std::is_default_constructible_v<T>); }
    ~ObjectPool() { for (T* p : all_) p->~T(); for (T* p : blocks_) ::operator delete(p); }
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    T* acquire() {
        std::lock_guard<std::mutex> g(mu_);
        if (free_.empty()) grow();
        T* p = free_.back();
        free_.pop_back();
        ++live_;
        return p;
    }
    void release(T* p) {
        if (!p) return;
        std::lock_guard<std::mutex> g(mu_);
        free_.push_back(p);
        if (live_ > 0) --live_;
    }
    [[nodiscard]] std::size_t live()     const { std::lock_guard<std::mutex> g(mu_); return live_; }
    [[nodiscard]] std::size_t reserved() const { std::lock_guard<std::mutex> g(mu_); return all_.size(); }

private:
    void grow() {
        T* chunk = static_cast<T*>(::operator new(sizeof(T) * BlockSize));
        blocks_.push_back(chunk);
        for (std::size_t i = 0; i < BlockSize; ++i) {
            new (chunk + i) T();
            all_.push_back(chunk + i);
            free_.push_back(chunk + i);
        }
    }
    mutable std::mutex mu_;
    std::vector<T*> blocks_, all_, free_;
    std::size_t live_ = 0;
};

/// RAII handle into an ObjectPool.
template <typename T, std::size_t BlockSize = 512>
class PooledPtr {
public:
    explicit PooledPtr(ObjectPool<T, BlockSize>& pool) : pool_(&pool), ptr_(pool.acquire()) {}
    ~PooledPtr() { if (pool_ && ptr_) pool_->release(ptr_); }
    PooledPtr(PooledPtr&& o) noexcept : pool_(o.pool_), ptr_(o.ptr_) { o.ptr_ = nullptr; o.pool_ = nullptr; }
    PooledPtr(const PooledPtr&) = delete;
    PooledPtr& operator=(const PooledPtr&) = delete;
    T* operator->() const { return ptr_; }
    T& operator*()  const { return *ptr_; }
    T* get()        const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }
private:
    ObjectPool<T, BlockSize>* pool_;
    T* ptr_;
};

} // namespace prism
