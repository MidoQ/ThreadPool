#include "threadpool.h"
#include <thread>
#include <iostream>

const int DEFAULT_TASK_MAX_COUNT = 1024;
const int DEFAULT_THREAD_MAX_COUNT = 20;
const int DEFAULT_MAX_IDLE_SEC = 60;

///////////////////////////////////////////////////// ThreadPool 方法实现

ThreadPool::ThreadPool()
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

ThreadPool::~ThreadPool()
{
    state_ = PoolState::STATE_EXITING;

    // 等待所有线程退出
    // 必须在获取锁后，再在 notEmpty_ 上通知等待的线程，保证该线程要么正在执行任务，要么处于等待状态
    std::unique_lock<std::mutex> lock(taskQueueMtx_);
    notEmpty_.notify_all();
    allExit_.wait(lock, [&]() -> bool { return threads_.empty(); });
}

void ThreadPool::setMode(PoolMode mode)
{
    if (!checkSetPermission()) {
        std::cerr << "CANNOT call set-function now! Nothing changed." << std::endl;
        return;
    }
    poolMode_ = mode;
}

void ThreadPool::setTaskMaxCount(size_t maxCount)
{
    if (!checkSetPermission()) {
        std::cerr << "CANNOT call set-function now! Nothing changed." << std::endl;
        return;
    }
    taskMaxCount_ = maxCount;
}

void ThreadPool::setThreadMaxCount(size_t maxCount)
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

void ThreadPool::setThreadIdleTimeout(size_t timeout)
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

void ThreadPool::start(int initThreadCount)
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

void ThreadPool::threadFunc(int threadId)
{
    std::cout << "Begin threadFunc() "
              << "tid: " << std::this_thread::get_id() << std::endl;

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
            std::cout << "Task execution over!" << std::endl;
        }

        idleThreadCount_++;
        lastTime = std::chrono::high_resolution_clock().now();
    }

    // 回收线程资源
    idleThreadCount_--;
    curThreadCount_--;
    threads_.erase(threadId);
    allExit_.notify_all();

    std::cout << "End   threadFunc() "
              << "tid: " << std::this_thread::get_id() << std::endl;
}

bool ThreadPool::checkSetPermission() const
{
    return state_ == PoolState::STATE_INIT;
}

///////////////////////////////////////////////////// Thread 方法实现

int Thread::threadIdBase = 0;

Thread::Thread(ThreadFunc func)
    : func_(func)
    , threadId_(threadIdBase++)
{
}

Thread::~Thread()
{
}

void Thread::start()
{
    // 创建一个线程对象，执行线程函数
    // 使用detach模式，防止thread对象销毁后线程被销毁，即thread对象与现线程本身分离
    std::thread t(func_, threadId_);
    t.detach();
}

int Thread::getId() const
{
    return threadId_;
}
