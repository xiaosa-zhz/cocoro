#include "cocoro/detached_task.hpp"
#include "cocoro/task.hpp"

#include <print>

cocoro::task<int> example_task() {
    std::println("{:{}}", co_await cocoro::corotrace::current(), 36);
    co_return 42;
}

cocoro::task<int> example_nested_task() {
    co_return co_await example_task();
}

cocoro::detached_task example_detached_task() {
    std::println("Result from example_task: {}", co_await example_nested_task());
}

int main() {
    example_detached_task().start();
}
