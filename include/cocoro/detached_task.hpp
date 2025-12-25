#pragma once
#ifndef COCORO_DETACHED_TASK_H
#define COCORO_DETACHED_TASK_H 1

#include <memory>
#include <exception>

#include "cocoro/env/trace.hpp"

namespace cocoro {

    namespace details {

        // Forward declaration
        struct detached_task_promise;

    } // namespace cocoro::details

    class detached_task_unhandled_exit_exception : public std::exception, public std::nested_exception
    {
    public:
        friend struct details::detached_task_promise;

        // satisfy standard exception requirements

        detached_task_unhandled_exit_exception() = default;
        detached_task_unhandled_exit_exception(const detached_task_unhandled_exit_exception&) = default;
        detached_task_unhandled_exit_exception(detached_task_unhandled_exit_exception&&) = default;
        detached_task_unhandled_exit_exception& operator=(const detached_task_unhandled_exit_exception&) = default;
        detached_task_unhandled_exit_exception& operator=(detached_task_unhandled_exit_exception&&) = default;

        ~detached_task_unhandled_exit_exception() override = default;

        const char* what() const noexcept override { return message; }

    private:
        constexpr static auto message = "Detached task exits with unhandled exception.";

        template<typename Promise>
        static void handle_destroyer(void* p) noexcept {
            std::coroutine_handle<Promise>::from_address(p).destroy();
        }

        template<typename Promise>
        explicit detached_task_unhandled_exit_exception(std::coroutine_handle<Promise> handle)
            // implicitly catch current exception via default init std::nested_exception
            : handle_holder(handle.address(), &handle_destroyer<Promise>)
        {}

        std::shared_ptr<void> handle_holder = nullptr;
    };

    // Forward declaration
    class detached_task;

    namespace details {

        // Forward declaration
        inline std::coroutine_handle<> detached_task_stopped(std::coroutine_handle<> handle) noexcept;

        struct detached_task_promise : private env::trace_env
        {
            using handle_type = std::coroutine_handle<detached_task_promise>;
            detached_task get_return_object() noexcept;
            void return_void() const noexcept {}
            std::suspend_always initial_suspend() const noexcept { return {}; }
            std::suspend_never final_suspend() const noexcept { return {}; } // coroutine destroyed on final suspend

            using env_type = env::trace_env;
            using env_type::query;
            using env_type::await_transform;

            const env_type& get_env() const noexcept {
                return static_cast<const env_type&>(*this);
            }

            void unhandled_exception() noexcept(false) {
                // propagate exception to caller, executor, or whatever
                throw detached_task_unhandled_exit_exception(handle_type::from_promise(*this));
            }

            std::coroutine_handle<> unhandled_stopped() noexcept {
                return detached_task_stopped(handle_type::from_promise(*this));
            }
        };

    } // namespace cocoro::details

    class [[nodiscard]] detached_task
    {
    public:
        using promise_type = details::detached_task_promise;
        friend promise_type;
    private:
        using handle_type = promise_type::handle_type;
        explicit detached_task(handle_type handle) noexcept : handle(handle) {}
        detached_task() = default;
    public:
        detached_task(const detached_task&) = delete;
        detached_task& operator=(const detached_task&) = delete;

        detached_task(detached_task&& other) noexcept
            : handle(std::exchange(other.handle, nullptr))
        {}

        detached_task& operator=(detached_task&& other) noexcept {
            detached_task().swap(other);
            return *this;
        }

        ~detached_task() { if (this->handle) { handle.destroy(); } }

        void swap(detached_task& other) noexcept {
            if (this == std::addressof(other)) { return; }
            std::ranges::swap(this->handle, other.handle);
        }

        // can only be called once
        // once called, detached_task object is not responsible for destroying the coroutine
        void start() && {
            std::exchange(this->handle, nullptr).resume();
        }

        [[nodiscard]]
        handle_type to_handle() && noexcept { return std::exchange(this->handle, nullptr); }

    private:
        handle_type handle = nullptr;
    };

    namespace details {

        inline detached_task cleanup(std::coroutine_handle<> handle) {
            handle.destroy();
            co_return;
        }

        inline std::coroutine_handle<> detached_task_stopped(std::coroutine_handle<> handle) noexcept {
            return cleanup(handle).to_handle();
        }

        inline detached_task detached_task_promise::get_return_object() noexcept {
            return detached_task(handle_type::from_promise(*this));
        }

    } // namespace mylib::details

} // namespace cocoro

#endif // COCORO_DETACHED_TASK_H
