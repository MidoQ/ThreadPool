#ifndef THREADPOOL_H
#define THREADPOOL_H

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
#include "fixedpool.h"
#include "cachedpoo.h"
#include "activepool.h"

/// @brief 线程池运行模式
enum class PoolMode {
    MODE_FIXED,
    MODE_CACHED,
    MODE_ACTIVE
};

/// @brief 线程池类型
class ThreadPool {
public:
    ThreadPool(PoolMode mode = PoolMode::MODE_FIXED)
        : pool_(nullptr)
    {
        switch (mode)
        {
        case PoolMode::MODE_FIXED:
            pool_ = std::move(std::make_unique<FixedPool>());
            break;
        case PoolMode::MODE_CACHED:
            pool_ = std::move(std::make_unique<CachedPool>());
            break;
        case PoolMode::MODE_ACTIVE:
            pool_ = std::move(std::make_unique<ActivePool>());
        default:
            break;
        }
    }

    ~ThreadPool()
    {
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// @brief 设置任务数量上限
    /// @param maxCount 任务数量上限
    void setTaskMaxCount(size_t maxCount)
    {
        pool_->setTaskMaxCount(maxCount);
    }

    /// @brief 设置 cached 模式下的线程数量上限
    /// @param maxCount 线程数量上限
    void setThreadMaxCount(size_t maxCount)
    {
        pool_->setThreadMaxCount(maxCount);
    }

    /// @brief 在 cached 模式下，设置额外线程被销毁的超时时间
    /// @details 在 cached 模式下，如果由于大量任务的提交而创建了额外的线程，
    ///          当任务队列空闲超过此时间后，额外的线程会自动退出
    /// @param timeout 超时时间，单位为秒
    void setThreadIdleTimeout(size_t timeout)
    {
        pool_->setThreadIdleTimeout(timeout);
    }

    /// @brief 开启线程池
    void start(int initThreadCount = 4)
    {
        pool_->start(initThreadCount);
    }

    /// @brief 给线程池提交任务
    /// @return 可能尚未完成的执行结果对象
    template <typename FUNC, typename... ARGS>
    auto submitTask(FUNC&& func, ARGS&&... args) -> std::future<decltype(func(args...))>
    {
        using RType = decltype(func(args...));     // 在运行时推断 Func 的返回值

        auto task = std::make_shared<std::packaged_task<RType()>>
                    (std::bind(std::forward<FUNC>(func), std::forward<ARGS>(args)...));
        Task taskWrapper([task]() { (*task)(); });

        try {
            pool_->submitTask(std::move(taskWrapper));
        } catch (char const* s) {
            std::string errMsg(s);
            if (errMsg == "PoolNotRunning") {
                std::cerr << __func__ << ": " << s << std::endl;
            } else if (errMsg == "TaskQueueOverflow") {
                std::cerr << __func__ << ": " << s << std::endl;
            }
            auto emptyTask = std::packaged_task<RType()>([]() -> RType { return RType(); });
            emptyTask();
            return emptyTask.get_future();
        }

        std::future<RType> res = task->get_future();

        return res;
    }

private:
    std::unique_ptr<BasePool> pool_;
};

#endif