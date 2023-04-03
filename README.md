# 基于C++14的线程池

## feature

+ 支持fixed/cached/active模式
  + fixed 模式：线程数固定，多个工作线程从**单个任务队列**中取任务，用户线程（任务提交线程）和工作线程之间通过互斥锁和条件变量进行通信。
  + cached 模式：在 fixed 模式的基础上，可根据任务数**动态增加线程**，并在长时间空闲后自动销毁。
  + active 模式：每个工作线程均具有自身的公共队列和私有队列，用户线程轮询各个线程并将任务提交到公共队列，尽量实现**负载均衡**；双队列通过**乒乓缓冲**的机制实现**读写分离**。
+ 支持任意参数和任意返回值的任务提交

## 环境要求

+ gcc 6.1 及以上
+ cmake

## 使用方法

在源文件中包含 threadpool.h 即可。

使用示例：

```cpp
// #include [some headers]
#include "threadpool.h"

int func(int a, int b, int c)
{
    return std::to_string(a) + std::to_string(b) + std::to_string(c);
}

int main()
{
    int tCount = std::thread::hardware_concurrency();
    ThreadPool pool(PoolMode::MODE_ACTIVE);
    pool.start(tCount);

    std::future<std::string> res1 = pool.submitTask(func, 1, 2, 3);
    std::future<int> res2 = pool.submitTask([](int num) -> int { return 123 + num; }, 666);

    return 0;
}

```

编译示例：

```sh
mkdir build
cd build
cmake ..
make
```

## 说明

代码中使用到的C++14语法仅限`std::make_unique`，若使用C++11，可参考以下实现：

```cpp
template<typename T, typename ...Args>
std::unique_ptr<T> make_unique( Args&& ...args ) {
return std::unique_ptr<T>( new T( std::forward<Args>(args)... ) );
}
```

## 性能优化

### 空任务测试

本项目原本仅实现了 fixed/cached 模式，新增加的 active 模式在大量空任务的场景下相比 fixed 模式有较大优势。

运行 test_empty_task.cpp 进行测试，结果如下：

> 测试环境：2核 CPU， 2G 内存， CentOS 8，编译等级为 Debug

```text
[Mode: fixed]
thread-count: 2 | task-count: 100      | time-cost: 0.765    ms
thread-count: 2 | task-count: 1000     | time-cost: 4.258    ms
thread-count: 2 | task-count: 10000    | time-cost: 38.184   ms
thread-count: 2 | task-count: 100000   | time-cost: 323.904  ms
thread-count: 2 | task-count: 1000000  | time-cost: 3195.76  ms

==================================================================

[Mode: active]
thread-count: 2 | task-count: 100      | time-cost: 0.304    ms
thread-count: 2 | task-count: 1000     | time-cost: 2.774    ms
thread-count: 2 | task-count: 10000    | time-cost: 35.581   ms
thread-count: 2 | task-count: 100000   | time-cost: 185.178  ms
thread-count: 2 | task-count: 1000000  | time-cost: 2020.53  ms

===================================================================

[Mode: cached]
thread-count: 2 | task-count: 100      | time-cost: 4.17     ms
thread-count: 2 | task-count: 1000     | time-cost: 4.329    ms
thread-count: 2 | task-count: 10000    | time-cost: 63.865   ms
thread-count: 2 | task-count: 100000   | time-cost: 647.432  ms
thread-count: 2 | task-count: 1000000  | time-cost: 6036.63  ms
```

### 性能差异分析

在 fixed 模式下，使用 perf 和火焰图分析性能瓶颈。

> [FlameGraph](https://github.com/brendangregg/FlameGraph) 程序来自 Brendan D. Gregg 开源。

#### on-cpu 测试

通过 perf 进行 on-cpu 采样，查看各函数占用 CPU 的比例。

```sh
perf record -g -o perf_oncpu.data ./bin/test_empty_task
perf script -i perf_oncpu.data > perf_oncpu.stack
../FlameGraph/stackcollapse-perf.pl perf_oncpu.stack > out_oncpu.stack
../FlameGraph/flamegraph.pl out_oncpu.stack > flame_oncpu.svg
```

![2023-03-28-16-31-18](https://midoq-image-host.oss-cn-hangzhou.aliyuncs.com/images/2023-03-28-16-31-18.png)

由火焰图可见，CPU 主要由 submitTask 和 threadFunc 占用，且比例相当。这对于线程池而言是合理的，它反映了用户提交任务时工作线程对任务队列的及时处理。

然而，由于空任务测试中并没有涉及大量计算或IO，性能瓶颈可能更多是由互斥锁的等待造成的，所以还需要进行 off-cpu 测试。

### off-cpu 测试

一般使用 bcc-tools 中的 offcputime 进行 off-cpu 采样。由于需要运行程序并追踪进程 PID ，所以将下面的语句写到脚本里一次运行：

```sh
./bin/test_empty_task &
/usr/share/bcc/tools/offcputime -df -p `pgrep -n test_empty_task` 4 > perf_test/out_offcpu.stack
```

然后生成火焰图：

```sh
../FlameGraph/flamegraph.pl --color=io out_oncpu.stack > flame_oncpu.svg
```

![2023-04-03-10-58-21](https://midoq-image-host.oss-cn-hangzhou.aliyuncs.com/images/2023-04-03-10-58-21.png)

可以看到， threadFunc 在 off-cpu 火焰图上占比很大。这意味着虽然用户线程一直在提交，任务队列里一直有任务存在，但是工作线程仍然有大量时间处于等待状态。这是因为任务队列仅有一把互斥锁，当用户在提交任务时，工作线程只能等待；当某个线程在取任务时，其他工作线程也只能等待，这些等待时间实际上是被浪费的。

active 模式的出现就是为了解决上述问题。相比 fixed/cached 模式，active 模式在读取任务时使用了自己实现的自旋锁，仅在公共队列和私有队列进行缓冲内容交换时会与任务读取产生竞争，一定程度上起到了读写分离的效果。

<div align='center'>
<figure>
    <img src="https://midoq-image-host.oss-cn-hangzhou.aliyuncs.com/images/2023-04-03-13-05-37.png" width=%>
    <figcaption>fixed 模式线程竞争关系示意图</figcaption>
</figure>
</div>

<div align='center'>
<figure>
    <img src="https://midoq-image-host.oss-cn-hangzhou.aliyuncs.com/images/2023-04-03-13-05-50.png" width=%>
    <figcaption>active 模式线程竞争关系示意图</figcaption>
</figure>
</div>
