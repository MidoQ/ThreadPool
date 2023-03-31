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
    ThreadPool pool;
    pool.setMode(PoolMode::MODE_CACHED);
    pool.start(4);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::future<int> res = pool.submitTask(taskFunc1, 1, 2, 3);
    std::future<std::string> str = pool.submitTask(taskFunc2, 1, "hello");
    std::future<std::string> str2 = pool.submitTask(taskFunc2, 2, "hello");
    std::future<std::string> str3 = pool.submitTask(taskFunc2, 3, "hello");
    std::future<std::string> str4 = pool.submitTask(taskFunc2, 4, "hello");
    std::future<std::string> str5 = pool.submitTask(taskFunc2, 5, "hello");
    std::future<std::string> str6 = pool.submitTask(taskFunc2, 6, "hello");
    std::future<std::string> str7 = pool.submitTask(taskFunc2, 7, "hello");
    std::future<std::string> str8 = pool.submitTask(taskFunc2, 8, "hello");

    std::cout << res.get() << std::endl;
    std::cout << str.get() << std::endl;
    std::cout << str2.get() << std::endl;
    std::cout << str3.get() << std::endl;
    std::cout << str4.get() << std::endl;
    std::cout << str5.get() << std::endl;
    std::cout << str6.get() << std::endl;
    std::cout << str7.get() << std::endl;
    std::cout << str8.get() << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(5));

    // char a = getchar();
    // std::cout << "a=" << a << std::endl;
    return 0;
}
