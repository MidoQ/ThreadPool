#ifndef FIXEDPOOL_H
#define FIXEDPOOL_H

#include "basepool.h"

class FixedPool : public BasePool {
const int DEFAULT_TASK_MAX_COUNT = 1000001;

public:
    FixedPool()
        : initThreadCount_(4)
        , idleThreadCount_(0)
        , curThreadCount_(0)
        , taskMaxCount_(DEFAULT_TASK_MAX_COUNT)
        , taskCount_(0)
    {
    }

    ~FixedPool()
    {
        state_ = PoolState::STATE_EXITING;

        // 等待所有线程退出
        // 必须在获取锁后，再在 notEmpty_ 上通知等待的线程，保证该线程要么正在执行任务，要么处于等待状态
        std::unique_lock<std::mutex> lock(taskQueueMtx_);
        notEmpty_.notify_all();
        allExit_.wait(lock, [&]() -> bool { return threads_.empty(); });
    }

    FixedPool(const FixedPool&) = delete;
    FixedPool& operator=(const FixedPool&) = delete;

    void setTaskMaxCount(size_t maxCount)
    {
        if (!checkSetPermission()) {
            std::cerr << "CANNOT call set-function now! Nothing changed." << std::endl;
            return;
        }
        taskMaxCount_ = maxCount;
    }

    void start(int initThreadCount = 4)
    {
        state_ = PoolState::STATE_RUNNING;
        initThreadCount_ = initThreadCount;

        // 创建线程对象
        for (int i = 0; i < initThreadCount_; i++) {
            auto ptr = std::make_unique<Thread>(std::bind(&FixedPool::threadFunc, this, std::placeholders::_1));
            threads_.emplace(ptr->getId(), std::move(ptr));
        }

        // 启动所有线程
        for (auto& th : threads_) {
            th.second->start();
        }

        curThreadCount_ = initThreadCount;
        idleThreadCount_ = initThreadCount;
    }

    void submitTask(Task&& task)
    {
        if (state_ != PoolState::STATE_RUNNING) {
            throw "PoolNotRunning";
        }

        // 获取互斥锁
        std::unique_lock<std::mutex> lock(taskQueueMtx_);

        // 等待任务队列不满，最多等待1秒
        bool waitSuccess = notFull_.wait_for(lock, std::chrono::seconds(1),
            [&]() -> bool { return taskQueue_.size() < taskMaxCount_; });
        if (!waitSuccess) {
            throw "TaskQueueOverflow";
        }

        // 将任务放入队列
        taskQueue_.emplace(std::forward<Task>(task));
        taskCount_++;

        // std::cout << "Task submit success!" << std::endl;

        // 通知挂起等待的线程，令其执行任务
        notEmpty_.notify_all();
    }

private:
    /// @brief 线程执行函数，消费任务队列中的任务
    void threadFunc(int threadId)
    {
        // std::cout << "Begin threadFunc() "
        //         << "tid: " << std::this_thread::get_id() << std::endl;

        bool isIdleTimeout = false;
        auto lastTime = std::chrono::high_resolution_clock().now();

        while (state_ == PoolState::STATE_RUNNING) {
            // 获取互斥锁
            std::unique_lock<std::mutex> lock(taskQueueMtx_);

            while (state_ == PoolState::STATE_RUNNING && taskQueue_.empty()) {
                // 无限等待任务队列不空
                notEmpty_.wait(lock, [&]() -> bool { return !taskQueue_.empty() || state_ == PoolState::STATE_EXITING; });
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

protected:
    size_t initThreadCount_;                        // 初始线程数量
    std::atomic<size_t> idleThreadCount_;           // 空闲线程数量
    std::atomic<size_t> curThreadCount_;            // 当前活动的线程数量
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;  // 线程列表

    size_t taskMaxCount_;                           // 任务队列中任务数量上限
    std::atomic<size_t> taskCount_;                 // 当前任务数量
    std::queue<Task> taskQueue_;                    // 任务队列

    std::mutex taskQueueMtx_;           // 线程取任务时，保证任务队列的线程安全
    std::condition_variable notFull_;   // 任务队列不满，供用户线程使用
    std::condition_variable notEmpty_;  // 任务队列不空，供线程池使用
    std::condition_variable allExit_;   // 供析构函数等待所有线程退出
};

#endif