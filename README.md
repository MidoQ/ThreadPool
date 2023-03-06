# 基于C++14的线程池

## feature

+ 支持fixed/cached模式
+ 支持任意参数和任意返回值的任务提交

## 环境要求

+ gcc 6.1 及以上
+ cmake

## 使用方法

```sh
mkdir build
cd build
cmake ..
make
```

## 说明

+ 在 threadpool.cpp 文件中可修改常量参数
+ 代码中使用到的C++14语法仅限`std::make_unique`，若使用C++11，可加入以下实现补充该缺陷：

```cpp
template<typename T, typename ...Args>
std::unique_ptr<T> make_unique( Args&& ...args ) {
return std::unique_ptr<T>( new T( std::forward<Args>(args)... ) );
}
```
