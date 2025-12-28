#pragma once
#ifndef COCORO_SYMMETRIC_TASK_H
#define COCORO_SYMMETRIC_TASK_H 1

#include "cocoro/utils/symres.hpp"
#include "cocoro/utils/basic_promise.hpp"
#include "cocoro/env/trace.hpp"
#include "cocoro/env/stop_token.hpp"

namespace cocoro {

    template<typename ResultType>
    class [[nodiscard]] task : private details::moveonly
    {
        struct task_promise;
    public:
        using promise_type = task_promise;
        using result_type = ResultType;
        using handle_type = std::coroutine_handle<promise_type>;

    private:
        struct task_promise :
            public basic_promise_base<env::trace_env, env::inplace_stop_env>,
            public symmetric_result<result_type>,
            public env::trace_await_base
        {
            task_promise() = default;

            task get_return_object() noexcept {
                return task(handle_type::from_promise(*this));
            }

            void set_suspension_point_info(std::source_location&& loc) noexcept {
                get_mut_env().set_suspension_point_info(std::move(loc));
            }

            continue_or_stop_awaitable final_suspend() const noexcept { return {}; }
        };

    public:
        task() = delete;

        ~task() {
            if (handle != nullptr) {
                handle.destroy();
            }
        }

        task(task&& other) noexcept :
            handle(std::exchange(other.handle, nullptr))
        {}

        task& operator=(task&& other) noexcept {
            auto(std::move(other)).swap(*this);
            return *this;
        }

        void swap(task& other) noexcept {
            std::ranges::swap(handle, other.handle);
        }

        class [[nodiscard]] task_awaiter
        {
        public:
            constexpr bool await_ready() const noexcept { return false; }

            template<typename Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> caller) {
                handle.promise().set_continuation(caller);
                return handle;
            }

            result_type await_resume() {
                return handle.promise().result();
            }

            ~task_awaiter() {
                if (handle != nullptr) {
                    handle.destroy();
                }
            }

        private:
            friend task;
            explicit task_awaiter(handle_type handle) noexcept :
                handle(handle)
            {}

            handle_type handle = nullptr;
        };

        task_awaiter operator co_await() && noexcept {
            return task_awaiter(std::exchange(handle, nullptr));
        }

    private:
        friend promise_type;
        explicit task(handle_type handle) noexcept :
            handle(handle)
        {}

        handle_type handle = nullptr;
    };

} // namespace cocoro

#endif // COCORO_SYMMETRIC_TASK_H
