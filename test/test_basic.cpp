#include <iostream>
#include <chrono>
#include <thread>
#include <future>
#include "threadpool.h"

typedef unsigned long long ull;

int taskFunc1(int a, int b, int c)
{
    std::cout << "taskFunc1 start..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));
    return a + b + c;
}

std::string taskFunc2(long a, const std::string& s)
{
    std::cout << "taskFunc2 start..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));
    return std::string("a=") + std::to_string(a) + s;
}

int main(int argc, char* argv[])
{
    // ThreadPool pool;
    ThreadPool pool(PoolMode::MODE_CACHED);
    pool.setThreadIdleTimeout(60);
    pool.setThreadMaxCount(10);
    pool.start(4);

    std::future<int> res1 = pool.submitTask(taskFunc1, 1, 2, 3);
    std::future<int> res2 = pool.submitTask([](int num) -> int { return 123 + 456 - num; }, 666);
    std::future<std::string> str1 = pool.submitTask(taskFunc2, 1, " hello");
    std::future<std::string> str2 = pool.submitTask(taskFunc2, 2, " hello");
    std::future<std::string> str3 = pool.submitTask(taskFunc2, 3, " hello");
    std::future<std::string> str4 = pool.submitTask(taskFunc2, 4, " hello");

    std::cout << res1.get() << std::endl;
    std::cout << res2.get() << std::endl;
    std::cout << str1.get() << std::endl;
    std::cout << str2.get() << std::endl;
    std::cout << str3.get() << std::endl;
    std::cout << str4.get() << std::endl;

    char a;
    std::cout << "Press any button to exit...";
    std::cin.get(a);
    return 0;
}
