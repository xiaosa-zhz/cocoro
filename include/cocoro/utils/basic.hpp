#pragma once
#ifndef COCORO_COROUTILS_H
#define COCORO_COROUTILS_H 1

#include <concepts>
#include <exception>
#include <coroutine>

namespace cocoro::details {

    struct pinned {
        pinned() = default;
        pinned(const pinned&) = delete;
        pinned(pinned&&) = delete;
        pinned& operator=(const pinned&) = delete;
        pinned& operator=(pinned&&) = delete;
        ~pinned() = default;
    };

    // TODO: replace with std::monostate when it is put into <utility>
    struct monostate {};

} // namespace cocoro::details

namespace cocoro {

    template<typename Promise>
    concept continuable_promise = requires (Promise& promise) {
        { promise.continuation() } noexcept -> std::convertible_to<std::coroutine_handle<>>;
    };

    struct continue_final_awaiter : std::suspend_always {
        template<continuable_promise Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept {
            return handle.promise().continuation();
        }
    };

    template<typename Promise>
    concept unhandled_stopped_aware_promise = requires (Promise& promise) {
        { promise.unhandled_stopped() } noexcept -> std::convertible_to<std::coroutine_handle<>>;
    };

    using stopped_handler_t = std::coroutine_handle<>(*)(void*) noexcept;

    [[noreturn]]
    constexpr std::coroutine_handle<> terminate_unhandled_stopped(void*) noexcept {
        std::terminate();
    }

    template<unhandled_stopped_aware_promise Promise>
    inline std::coroutine_handle<> default_unhandled_stopped_handler(void* addr) noexcept {
        return std::coroutine_handle<Promise>::from_address(addr)
            .promise().unhandled_stopped();
    }

} // namespace cocoro

#endif // COCORO_COROUTILS_H
