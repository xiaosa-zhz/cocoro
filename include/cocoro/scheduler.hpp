#pragma once
#ifndef COCORO_SCHEDULER_H
#define COCORO_SCHEDULER_H 1

#include <concepts>
#include <type_traits>
#include <utility>
#include <coroutine>

#include "cocoro/utils/basic.hpp"

namespace cocoro {

    template<typename Sch>
    concept scheduler = std::copy_constructible<Sch>
        && std::is_nothrow_constructible_v<Sch>
        && requires (const Sch& sch, std::coroutine_handle<> task) {
            { sch.schedule(task) } noexcept -> std::convertible_to<bool>;
        };

    template<scheduler Scheduler>
    class schedule_on_awaiter : private details::pinned
    {
    public:
        schedule_on_awaiter() = delete;

        explicit schedule_on_awaiter(Scheduler sch) noexcept
            : sch(std::move(sch))
        {}

        static constexpr bool await_ready() noexcept { return false; }

        template<typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> cur) noexcept {
            const bool success = sch.schedule(cur);
            // write is_rescheduled only if scheduling is failed
            // so that there is no data race
            if (!success) { is_rescheduled = false; }
            return success;
        }

        [[nodiscard]]
        bool await_resume() const noexcept { return is_rescheduled; }

    private:
        Scheduler sch;
        bool is_rescheduled = true;
    };

    template<scheduler Scheduler>
    constexpr schedule_on_awaiter<Scheduler> schedule_on(Scheduler sch) noexcept {
        return schedule_on_awaiter<Scheduler>{std::move(sch)};
    }

} // namespace cocoro

#endif // COCORO_SCHEDULER_H
