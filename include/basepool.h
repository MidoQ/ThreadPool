#ifndef BASEPOOL_H
#define BASEPOOL_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <future>
#include "basicthread.h"

enum class PoolState {
    STATE_INIT,
    STATE_RUNNING,
    STATE_EXITING
};

using Task = std::function<void()>;

class BasePool {
protected:
    std::atomic<PoolState> state_; // 线程池的状态

public:
    BasePool()
        : state_(PoolState::STATE_INIT)
    {
    }

    virtual ~BasePool()
    {
    }

    BasePool(BasePool&) = delete;
    BasePool& operator=(BasePool&) = delete;

    virtual void setTaskMaxCount(size_t maxCount)
    {
        std::cerr << __func__ << "(): Unsupported operation!" << std::endl;
    }

    virtual void setThreadMaxCount(size_t maxCount)
    {
        std::cerr << __func__ << "(): Unsupported operation!" << std::endl;
    }

    virtual void setThreadIdleTimeout(size_t timeout)
    {
        std::cerr << __func__ << "(): Unsupported operation!" << std::endl;
    }

    virtual void start(int initThreadCount) = 0;

    virtual void submitTask(Task&& task) = 0;

protected:
    /// @brief 检查当前是否允许调用 set 类方法
    bool checkSetPermission() const
    {
        return state_ == PoolState::STATE_INIT;
    }
};

#endif