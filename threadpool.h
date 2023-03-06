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

/// @brief 线程池运行模式
enum class PoolMode {
    MODE_FIXED,
    MODE_CACHED
};

enum class PoolState {
    STATE_INIT,
    STATE_RUNNING,
    STATE_EXITING
};

/// @brief 线程类型
class Thread {
public:
    // 线程函数对象的类型，实际的线程函数由ThreadPool绑定
    using ThreadFunc = std::function<void(int)>;

    Thread(ThreadFunc func);
    ~Thread();

    /// @brief 启动线程
    void start();

    /// @brief 获取线程ID
    /// @return 线程ID
    int getId() const;

private:
    static int threadIdBase;
    int threadId_;
    ThreadFunc func_;
};

/// @brief 线程池类型
class ThreadPool {
public:
    ThreadPool();
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// @brief 设置线程池的工作模式
    /// @param mode PoolMode::MODE_FIXED | PoolMode::MODE_CACHED
    void setMode(PoolMode mode);

    /// @brief 设置任务数量上限
    /// @param maxCount 任务数量上限 (默认值为 1024)
    void setTaskMaxCount(size_t maxCount);

    /// @brief 设置 cached 模式下的线程数量上限
    /// @param maxCount 线程数量上限 (默认值为 20)
    void setThreadMaxCount(size_t maxCount);

    /// @brief 在 cached 模式下，设置额外线程被销毁的超时时间
    /// @details 在 cached 模式下，如果由于大量任务的提交而创建了额外的线程，
    ///          当任务队列空闲超过此时间后，额外的线程会自动退出
    /// @param timeout 超时时间，单位为秒
    void setThreadIdleTimeout(size_t timeout);

    /// @brief 开启线程池
    void start(int initThreadCount = 4);

    /// @brief 给线程池提交任务
    /// @param pTask 提交的任务
    /// @return 可能尚未完成的执行结果对象
    // Result submitTask(std::shared_ptr<Task> pTask);
    template <typename FUNC, typename... ARGS>
    auto submitTask(FUNC&& func, ARGS&&... args) -> std::future<decltype(func(args...))>
    {
        using RType = decltype(func(args...));     // 在运行时推断 Func 的返回值

        if (state_ != PoolState::STATE_RUNNING) {
            std::cerr << "Task submit failed! Thread Poool is not running!" << std::endl;
            auto task = std::packaged_task<RType()>([]() -> RType { return RType(); });
            task();
            return task.get_future();
        }

        auto task = std::make_shared<std::packaged_task<RType()>>
                    (std::bind(std::forward<FUNC>(func), std::forward<ARGS>(args)...));
        std::future<RType> result = task->get_future();

        // 获取互斥锁
        std::unique_lock<std::mutex> lock(taskQueueMtx_);

        // 等待任务队列不满，最多等待1秒
        bool waitSuccess = notFull_.wait_for(lock, std::chrono::seconds(1),
            [&]() -> bool { return taskQueue_.size() < taskMaxCount_; });
        if (!waitSuccess) {
            std::cerr << "Task queue is full, task submission failed!" << std::endl;
            auto task = std::packaged_task<RType()>([]() -> RType { return RType(); });
            task();
            return task.get_future();
        }

        // 将任务放入队列
        taskQueue_.emplace([task]() { (*task)(); });
        taskCount_++;

        std::cout << "Task submit success!" << std::endl;

        // 通知挂起等待的线程，令其执行任务
        notEmpty_.notify_all();

        // 在 cached 模式下，可能需要创建新线程
        if (poolMode_ == PoolMode::MODE_CACHED
            && taskCount_ > idleThreadCount_ && curThreadCount_ < maxThreadCount_) {
            auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
            int newThreadId = ptr->getId();
            threads_.emplace(newThreadId, std::move(ptr));
            threads_[newThreadId]->start();
            curThreadCount_++;
            idleThreadCount_++;
        }

        // 返回任务执行结果（可能尚未执行完）
        return result;
    }

private:
    /// @brief 线程执行函数，消费任务队列中的任务
    void threadFunc(int threadId);

    /// @brief 检查当前是否允许调用 set 类方法
    bool checkSetPermission() const;

private:
    PoolMode poolMode_; // 线程池的工作模式

    size_t initThreadCount_;                        // 初始线程数量
    size_t maxThreadCount_;                         // 线程数量最大值
    size_t threadMaxIdleSec_;                       // cached 模式下销毁额外线程的超时时间
    std::atomic<size_t> idleThreadCount_;           // 空闲线程数量
    std::atomic<size_t> curThreadCount_;            // 当前活动的线程数量
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;  // 线程列表

    using Task = std::function<void()>;

    size_t taskMaxCount_;                           // 任务队列中任务数量上限
    std::atomic<uint> taskCount_;                   // 当前任务数量
    std::queue<Task> taskQueue_;   // 任务队列，使用智能指针保持Task的生命周期

    std::mutex taskQueueMtx_;           // 线程取任务时，保证任务队列的线程安全
    std::condition_variable notFull_;   // 任务队列不满，供用户线程使用
    std::condition_variable notEmpty_;  // 任务队列不空，供线程池使用
    std::condition_variable allExit_;   // 供析构函数等待所有线程退出

    std::atomic<PoolState> state_; // 线程池的状态
};

#endif