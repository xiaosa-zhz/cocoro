#include "cocoro/detached_task.hpp"
#include "cocoro/task.hpp"

#include <print>

cocoro::task<int> example_task() {
    std::println("{:36}", co_await cocoro::corotrace::current());
    co_return 42;
}

cocoro::task<int> example_nested_task() {
    co_return co_await example_task();
}

cocoro::detached_task example_detached_task() {
    std::print("Result from example_task: {}\n", co_await example_nested_task());
}

int main() {
    example_detached_task().start();
}
