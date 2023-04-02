#ifndef BASICTHREAD_H
#define BASICTHREAD_H

#include <thread>

/// @brief 线程类型
class Thread {
public:
    // 线程函数对象的类型，实际的线程函数由ThreadPool绑定
    using ThreadFunc = std::function<void(int)>;

    Thread(ThreadFunc func)
    : func_(func)
    , threadId_(threadIdBase++)
    {
    }

    virtual ~Thread()
    {
    }

    /// @brief 启动线程
    void start()
    {
        // 创建一个线程对象，执行线程函数
        // 使用detach模式，防止thread对象销毁后线程被销毁，即thread对象与线程本身分离
        std::thread t(func_, threadId_);
        t.detach();
    }

    /// @brief 获取线程ID
    /// @return 线程ID
    int getId() const
    {
        return threadId_;
    }

protected:
    static int threadIdBase;
    int threadId_;
    ThreadFunc func_;
};

int Thread::threadIdBase = 0;

#endif
