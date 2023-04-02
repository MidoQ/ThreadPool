#ifndef ACTIVEPOOL_H
#define ACTIVEPOOL_H

#include "util.h"
#include "basepool.h"

class ThreadWithDQ : public Thread {

public:
    ThreadWithDQ(ThreadFunc func)
        : Thread(func)
        , publicTaskCount_(0)
        , privateTaskCount_(0)
    {
        publicQ_ = &TaskQ1_;
        privateQ_ = &TaskQ2_;
    }

    ~ThreadWithDQ()
    {
        // TODO
    }

    void setTaskMaxCount(size_t maxCount)
    {
        // TODO
    }

    static size_t getTaskMaxCount()
    {
        return taskMaxCount_;
    }

    size_t getTaskCount() const
    {
        return publicTaskCount_ + privateTaskCount_;
    }

    size_t getPublicTaskCount() const
    {
        return publicTaskCount_;
    }

    void giveTask(Task&& task)
    {
        // 加锁并将任务加到公共任务队列
        // 仅当线程恰好在交换公私有队列时会发生
        spinlock_guard lock_g(pubLock);
        publicQ_->emplace(std::forward<Task>(task));
        publicTaskCount_++;
    }

    /// @brief 尝试交换队列
    /// @return  0 私有队列未消费完，不交换;
    /// @return  1 私有队列已消费完，公有队列不为空，成功交换;
    /// @return -1 私有队列已消费完，公有队列也为空，不交换.
    int trySwapQ()
    {
        if (privateTaskCount_ == 0) {
            if (publicTaskCount_ != 0) {
                swapQ();
                return 1;
            } else {
                return -1;
            }
        }
        return 0;
    }

    void consumeTasks()
    {
        spinlock_guard lock_g(priLock);
        while (!privateQ_->empty()) {
            privateQ_->front()();
            privateQ_->pop();
        }
        privateTaskCount_ = 0;
    }

private:
    void swapQ()
    {
        // 加锁并交换公共任务队列和私有任务队列
        spinlock_guard lock_g1(pubLock);
        spinlock_guard lock_g2(priLock);

        swap(publicQ_, privateQ_);

        size_t tmp = publicTaskCount_.exchange(privateTaskCount_.load());
        privateTaskCount_ = tmp;
    }

protected:
    static size_t taskMaxCount_;
    std::atomic<size_t> publicTaskCount_, privateTaskCount_;
    std::queue<Task>*publicQ_, *privateQ_;
    std::queue<Task> TaskQ1_, TaskQ2_;

    spinlock pubLock;     // 公共队列锁
    spinlock priLock;     // 私有队列锁

public:
    constexpr static int DEFAULT_TASK_MAX_COUNT = 500001;  // 5e5
};

size_t ThreadWithDQ::taskMaxCount_ = ThreadWithDQ::DEFAULT_TASK_MAX_COUNT;

class ActivePool : public BasePool {
    // const int DEFAULT_TASK_MAX_COUNT = 1000001; // 1e6
    const int DEFAULT_THREAD_MAX_COUNT = 32;

public:
    ActivePool()
        : initThreadCount_(4)
        // , maxThreadCount_(DEFAULT_THREAD_MAX_COUNT)
        , curThreadCount_(0)
    {
        threads_.reserve(DEFAULT_THREAD_MAX_COUNT);
    }

    ~ActivePool()
    {
        state_ = PoolState::STATE_EXITING;

        std::unique_lock<std::mutex> lock(waitTaskMtx_);
        notEmpty_.notify_all();
        allExit_.wait(lock, [&]() -> bool { return curThreadCount_ == 0; });
    }

    void setTaskMaxCount(size_t maxCount)
    {
        // TODO
    }

    void start(int initThreadCount = 4)
    {
        initThreadCount_ = initThreadCount;

        // 创建线程对象
        for (int i = 0; i < initThreadCount_; i++) {
            auto ptr = std::make_unique<ThreadWithDQ>(std::bind(&ActivePool::threadFunc, this, std::placeholders::_1));
            threads_.emplace_back(std::move(ptr));
        }

        // 启动所有线程
        for (auto& th : threads_) {
            th->start();
        }

        curThreadCount_ = initThreadCount;
        state_ = PoolState::STATE_RUNNING;
    }

    void submitTask(Task&& task)
    {
        // notEmpty_ 仅用于唤醒工作线程，不必获取锁
        if (trySubmitTask(std::forward<Task>(task))) {
            notEmpty_.notify_all();
            return;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cerr << "ThreadPool is busy, waiting for 1 second..." << std::endl;

        if (trySubmitTask(std::forward<Task>(task))) {
            notEmpty_.notify_all();
            return;
        }

        throw "TaskQueueOverflow";
    }

private:
    bool trySubmitTask(Task&& task)
    {
        int leastBusyTid = 0;
        int minTaskCount = threads_[0]->getPublicTaskCount(); // getTaskCount();
        for (int i = 1; i < curThreadCount_; ++i) {
            size_t count = threads_[i]->getPublicTaskCount();  //getTaskCount();
            if (count < minTaskCount) {
                leastBusyTid = i;
                minTaskCount = count;
            }
        }

        if (minTaskCount == ThreadWithDQ::getTaskMaxCount()) {
            return false;
        }

        threads_[leastBusyTid]->giveTask(std::forward<Task>(task));
        return true;
    }

    void threadFunc(int threadId)
    {
        int id = threadId - threads_[0]->getId();
        ThreadWithDQ* thd = threads_[id].get();

        while (state_ == PoolState::STATE_RUNNING) {
            if (state_ == PoolState::STATE_RUNNING && thd->trySwapQ() == -1) {
                // 无限期等待公有队列中有任务
                std::unique_lock<std::mutex> lock(waitTaskMtx_);
                notEmpty_.wait(lock,  [&]() -> bool {
                    return thd->getPublicTaskCount() > 0 || state_ == PoolState::STATE_EXITING;
                });
            }

            if (state_ == PoolState::STATE_EXITING) {
                break;
            }

            thd->consumeTasks();
        }

        threads_[id] = nullptr;
        curThreadCount_--;
        allExit_.notify_all();
    }

protected:
    size_t initThreadCount_;                        // 初始线程数量
    // size_t maxThreadCount_;                      // 线程数量最大值
    std::atomic<size_t> curThreadCount_;            // 当前活动的线程数量
    std::vector<std::unique_ptr<ThreadWithDQ>> threads_;  // 线程列表

    std::mutex waitTaskMtx_;         // 线程取任务时，保证任务队列的线程安全
    std::condition_variable notEmpty_;  // 任务队列不空，用于唤醒工作线程
    std::condition_variable allExit_;   // 供析构函数等待所有线程退出
};

#endif