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

const int DEFAULT_TASK_MAX_COUNT = 1024;
const int DEFAULT_THREAD_MAX_COUNT = 20;
const int DEFAULT_MAX_IDLE_SEC = 60;

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

/// @brief 线程池类型
class ThreadPool {
public:
    ThreadPool()
        : poolMode_(PoolMode::MODE_FIXED)
        , initThreadCount_(4)
        , maxThreadCount_(DEFAULT_THREAD_MAX_COUNT)
        , threadMaxIdleSec_(DEFAULT_MAX_IDLE_SEC)
        , idleThreadCount_(0)
        , curThreadCount_(0)
        , taskCount_(0)
        , taskMaxCount_(DEFAULT_TASK_MAX_COUNT)
        , state_(PoolState::STATE_INIT)
    {
    }

    ~ThreadPool()
    {
        state_ = PoolState::STATE_EXITING;

        // 等待所有线程退出
        // 必须在获取锁后，再在 notEmpty_ 上通知等待的线程，保证该线程要么正在执行任务，要么处于等待状态
        std::unique_lock<std::mutex> lock(taskQueueMtx_);
        notEmpty_.notify_all();
        allExit_.wait(lock, [&]() -> bool { return threads_.empty(); });
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// @brief 设置线程池的工作模式
    /// @param mode PoolMode::MODE_FIXED | PoolMode::MODE_CACHED
    void setMode(PoolMode mode)
    {
        if (!checkSetPermission()) {
            std::cerr << "CANNOT call set-function now! Nothing changed." << std::endl;
            return;
        }
        poolMode_ = mode;
    }

    /// @brief 设置任务数量上限
    /// @param maxCount 任务数量上限 (默认值为 1024)
    void setTaskMaxCount(size_t maxCount)
    {
        if (!checkSetPermission()) {
            std::cerr << "CANNOT call set-function now! Nothing changed." << std::endl;
            return;
        }
        taskMaxCount_ = maxCount;
    }

    /// @brief 设置 cached 模式下的线程数量上限
    /// @param maxCount 线程数量上限 (默认值为 20)
    void setThreadMaxCount(size_t maxCount)
    {
        if (!checkSetPermission()) {
            std::cerr << "CANNOT call set-function now! Nothing changed." << std::endl;
            return;
        }

        if (poolMode_ == PoolMode::MODE_FIXED) {
            std::cout << "Threadpool is now in fixed-mode! Nothing changed." << std::endl;
            return;
        }

        maxThreadCount_ = maxCount;
    }

    /// @brief 在 cached 模式下，设置额外线程被销毁的超时时间
    /// @details 在 cached 模式下，如果由于大量任务的提交而创建了额外的线程，
    ///          当任务队列空闲超过此时间后，额外的线程会自动退出
    /// @param timeout 超时时间，单位为秒
    void setThreadIdleTimeout(size_t timeout)
    {
        if (!checkSetPermission()) {
            std::cerr << "CANNOT call set-function now! Nothing changed." << std::endl;
            return;
        }

        if (poolMode_ == PoolMode::MODE_FIXED) {
            std::cout << "Threadpool is now in fixed-mode! Nothing changed." << std::endl;
            return;
        }

        threadMaxIdleSec_ = timeout;
    }

    /// @brief 开启线程池
    void start(int initThreadCount = 4)
    {
        state_ = PoolState::STATE_RUNNING;
        initThreadCount_ = initThreadCount;

        // 创建线程对象
        for (int i = 0; i < initThreadCount_; i++) {
            auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
            threads_.emplace(ptr->getId(), std::move(ptr));
        }

        // 启动所有线程
        for (auto& th : threads_) {
            th.second->start();
        }

        curThreadCount_ = initThreadCount;
        idleThreadCount_ = initThreadCount;
    }

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

        // std::cout << "Task submit success!" << std::endl;

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
    void threadFunc(int threadId)
    {
        // std::cout << "Begin threadFunc() "
        //         << "tid: " << std::this_thread::get_id() << std::endl;

        bool isIdleTimeout = false;
        auto lastTime = std::chrono::high_resolution_clock().now();

        auto checkThreadIdleTimeout = [&]() {
            if (curThreadCount_ == initThreadCount_) {
                return;
            }
            auto nowTime = std::chrono::high_resolution_clock().now();
            auto idleDuration = std::chrono::duration_cast<std::chrono::seconds>(nowTime - lastTime);
            if (idleDuration.count() > threadMaxIdleSec_) {
                isIdleTimeout = true;
            }
        };

        while (state_ == PoolState::STATE_RUNNING) {
            // 获取互斥锁
            std::unique_lock<std::mutex> lock(taskQueueMtx_);

            while (state_ == PoolState::STATE_RUNNING && taskQueue_.empty()) {
                if (poolMode_ == PoolMode::MODE_CACHED) {
                    // 每等待1秒时间后返回，检查是否超过最大空闲时间
                    std::cv_status retStatus = notEmpty_.wait_for(lock, std::chrono::seconds(1));
                    if (retStatus == std::cv_status::timeout) {
                        // std::cout << "Thread is idle for 1 seconds...\n";
                        checkThreadIdleTimeout();
                        if (isIdleTimeout) {
                            break;
                        }
                    }
                } else {
                    // 无限等待任务队列不空
                    notEmpty_.wait(lock, [&]() -> bool { return !taskQueue_.empty() || state_ == PoolState::STATE_EXITING; });
                }
            }

            if (state_ == PoolState::STATE_EXITING || isIdleTimeout) {
                break;
            }

            isIdleTimeout = false;
            idleThreadCount_--;

            // 从任务队列取一个任务
            auto pTask = taskQueue_.front();
            taskQueue_.pop();
            taskCount_--;

            // 取完任务后，需要通知其他线程继续取任务，通知用户线程任务队列已经不满
            // 同时应当立即释放锁，再执行任务
            if (!taskQueue_.empty()) {
                notEmpty_.notify_all();
            }
            notFull_.notify_all();
            lock.unlock();

            // 执行任务
            if (pTask != nullptr) {
                pTask();
                // std::cout << "Task execution over!" << std::endl;
            }

            idleThreadCount_++;
            lastTime = std::chrono::high_resolution_clock().now();
        }

        // 回收线程资源
        idleThreadCount_--;
        curThreadCount_--;
        threads_.erase(threadId);
        allExit_.notify_all();

        // std::cout << "End   threadFunc() "
        //         << "tid: " << std::this_thread::get_id() << std::endl;
    }

    /// @brief 检查当前是否允许调用 set 类方法
    bool checkSetPermission() const
    {
        return state_ == PoolState::STATE_INIT;
    }

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