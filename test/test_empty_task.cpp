#include <thread>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <memory>
#include <string>
#include "threadpool.h"

using std::cout;
using std::endl;
using namespace std::this_thread;
using namespace std::chrono;

#define TASK_MIN_CNT 100
#define TASK_MAX_CNT 1000000

template <typename F, typename... Args>
double runAndTiming(F&& func, Args&&... args)
{
    auto startTime = steady_clock::now();
    func(std::forward<Args>(args)...);
    auto endTime = steady_clock::now();
    double ms = duration_cast<microseconds>(endTime - startTime).count() / 1000.0;
    return ms;
}

void test_tasks(PoolMode mode = PoolMode::MODE_FIXED)
{
    int thCount = std::thread::hardware_concurrency();

    ThreadPool pool(mode);
    // pool.setTaskMaxCount(TASK_MAX_CNT);
    // pool.setThreadMaxCount(16);
    pool.start(thCount);

    auto foo = [&](int taskNum) {
        for (int i = 0; i < taskNum; ++i)
            pool.submitTask([]() {});
    };

    for (int taskCnt = TASK_MIN_CNT; taskCnt <= TASK_MAX_CNT; taskCnt *= 10) {
        double msTime = runAndTiming<>(foo, taskCnt);
        cout << "thread-count: " << thCount << " | "
             << "task-count: " << std::left << std::setw(8) << taskCnt << " | "
             << "time-cost: " << std::setw(8) << msTime << " ms" << endl;
    }
}

int main(int argc, char* argv[])
{
    std::string s = "\n";
    s.append(80, '=');
    s.append("\n");

    cout << "[Mode: fixed]" << endl;
    test_tasks(PoolMode::MODE_FIXED);

    sleep_for(seconds(5));
    cout << s << endl;

    cout << "[Mode: active]" << endl;
    test_tasks(PoolMode::MODE_ACTIVE);

    sleep_for(seconds(5));
    cout << s << endl;

    cout << "[Mode: cached]" << endl;
    test_tasks(PoolMode::MODE_CACHED);

    return 0;
}
