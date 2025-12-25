#pragma once
#ifndef COCORO_UTILITYS_SYMMETRIC_PROMISE_H
#define COCORO_UTILITYS_SYMMETRIC_PROMISE_H 1

#include <type_traits>
#include <coroutine>
#include <optional>

#include "cocoro/utils/basic.hpp"
#include "cocoro/env/env.hpp"

namespace cocoro {

    template<typename... Envs>
    class basic_promise_base : private details::pinned
    {
    public:
        basic_promise_base() = default;

        using env_type = env::composed_environment<Envs...>;

        const env_type& get_env() const noexcept { return *env; }

        env_type& get_mut_env() noexcept { return *env; }

        template<env::has_query<env_type> Query>
        env::query_result_t<env_type, Query> query(Query q) const noexcept {
            return get_env().query(q);
        }

        template<typename OtherPromise>
            requires (not std::same_as<OtherPromise, void>)
        void set_continuation(std::coroutine_handle<OtherPromise> handle) noexcept {
            if constexpr (env::env_aware<OtherPromise>) {
                env.emplace(handle.promise().get_env());
            } else if constexpr (std::is_default_constructible_v<env_type>) {
                env.emplace();
            }

            if constexpr (unhandled_stopped_aware_promise<OtherPromise>) {
                stopped_handler = &default_unhandled_stopped_handler<OtherPromise>;
            } else {
                stopped_handler = &terminate_unhandled_stopped;
            }

            cont = handle;
        }

        std::coroutine_handle<> continuation() const noexcept { return cont; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        continue_final_awaiter final_suspend() noexcept { return {}; }

    private:
        std::coroutine_handle<> cont = nullptr;
        stopped_handler_t stopped_handler = &terminate_unhandled_stopped;
        std::optional<env_type> env = std::nullopt;
    };

} // namespace cocoro

#endif // COCORO_UTILITYS_SYMMETRIC_PROMISE_H
