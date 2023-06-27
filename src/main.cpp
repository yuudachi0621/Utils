#include <iostream>
#include <chrono>
#include "ThreadPool.h"

void testThreadPool()
{
    util::ThreadPool pool(4);
    auto result = pool.enqueue([](int answer) { return answer; }, 42);
    std::cout << result.get() << std::endl;
}

int main()
{
    testThreadPool();
    return 0;
}