#ifndef UTIL_H
#define UTIL_H

#include <atomic>
#include <memory>

class spinlock {
private:
    std::atomic_flag flag;

public:
    spinlock()
        : flag(ATOMIC_FLAG_INIT)
    {
    }

    spinlock(spinlock&) = delete;
    spinlock& operator=(spinlock&) = delete;

    void lock()
    {
        while (flag.test_and_set(std::memory_order_acquire))
            ;
    }

    void unlock()
    {
        flag.clear();
    }

    /// @brief 如果锁可用（未被占有），则返回 true
    bool try_lock()
    {
        return !flag.test_and_set();
    }
};

class spinlock_guard {
private:
    spinlock* lock_;

public:
    explicit spinlock_guard(spinlock& lock)
        : lock_(&lock)
    {
        lock_->lock();
    }

    spinlock_guard(spinlock_guard&) = delete;
    spinlock_guard& operator=(spinlock_guard&) = delete;

    ~spinlock_guard()
    {
        lock_->unlock();
        lock_ = nullptr;
    }
};

#endif