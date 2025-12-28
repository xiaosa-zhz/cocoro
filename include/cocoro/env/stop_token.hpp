#pragma once
#ifndef COCORO_ENVIRONMENT_CANCELLATION_H
#define COCORO_ENVIRONMENT_CANCELLATION_H 1

#include <concepts>
#include <type_traits>
#include <atomic>
#include <utility>

#include "cocoro/utils/basic.hpp"
#include "./env.hpp"

namespace cocoro {

    template<typename StopToken>
    concept eligible_stop_token = std::default_initializable<StopToken>
        && std::copy_constructible<StopToken>
        && std::destructible<StopToken>
        && std::is_nothrow_copy_constructible_v<StopToken>
        && std::is_nothrow_copy_assignable_v<StopToken>
        && std::is_nothrow_swappable_v<StopToken>
        && std::equality_comparable<StopToken>
        && requires (const StopToken& token) {
            { token.stop_requested() } noexcept -> std::convertible_to<bool>;
            { token.stop_possible() } noexcept -> std::convertible_to<bool>;
        };

    template<typename StopSource>
    concept eligible_stop_source = std::default_initializable<StopSource>
        && std::destructible<StopSource>
        && requires (const StopSource& source, StopSource& stop_source) {
            { source.get_token() } noexcept -> eligible_stop_token;
            { source.stop_possible() } noexcept -> std::convertible_to<bool>;
            { source.stop_requested() } noexcept -> std::convertible_to<bool>;
            { stop_source.request_stop() } noexcept -> std::convertible_to<bool>;
        };

    template<eligible_stop_source StopSource>
    using stop_token_t = decltype(std::declval<const StopSource&>().get_token());

    struct never_stop_token {
        static constexpr bool stop_requested() noexcept { return false; }
        static constexpr bool stop_possible() noexcept { return false; }
        friend constexpr bool operator==(const never_stop_token&, const never_stop_token&) noexcept {
            return true;
        }
    };

    static_assert(eligible_stop_token<never_stop_token>);

    // Forward declaration
    class inplace_stop_source;

    class inplace_stop_token
    {
    public:
        constexpr inplace_stop_token() = default;
        constexpr inplace_stop_token(const inplace_stop_token&) = default;
        constexpr inplace_stop_token& operator=(const inplace_stop_token&) = default;
        friend bool operator==(const inplace_stop_token&, const inplace_stop_token&) = default;

        constexpr void swap(inplace_stop_token& other) noexcept {
            std::ranges::swap(source, other.source);
        }

        bool stop_requested() const noexcept;
        bool stop_possible() const noexcept { return source != nullptr; }

    private:
        friend class inplace_stop_source;
        constexpr explicit inplace_stop_token(const inplace_stop_source* source) noexcept
            : source(source)
        {}

        const inplace_stop_source* source = nullptr;
    };

    class inplace_stop_source : private details::pinned
    {
    public:
        constexpr inplace_stop_source() = default;

        constexpr inplace_stop_token get_token() const noexcept {
            return inplace_stop_token(this);
        }

        static constexpr bool stop_possible() noexcept { return true; }

        bool stop_requested() const noexcept {
            return stopped.test(std::memory_order_acquire);
        }

        bool request_stop() noexcept {
            return !stopped.test_and_set(std::memory_order_acq_rel);
        }

    private:
        std::atomic_flag stopped = false;
    };

    inline bool inplace_stop_token::stop_requested() const noexcept {
        return source != nullptr && source->stop_requested();
    }

    static_assert(eligible_stop_token<inplace_stop_token>);
    static_assert(eligible_stop_source<inplace_stop_source>);

} // namespace cocoro

namespace cocoro::details {

    struct get_stop_token_query_fn {
        template<typename Env>
        static eligible_stop_token auto operator()(const Env& env) noexcept {
            if constexpr (env::queryable<Env, get_stop_token_query_fn>) {
                return env.query(get_stop_token_query_fn{});
            } else {
                return never_stop_token{};
            }
        }
    };

    template<typename OtherEnv>
    concept inplace_stop_compatible = env::queryable<OtherEnv, get_stop_token_query_fn>
        && std::convertible_to<
            env::query_result_t<OtherEnv, get_stop_token_query_fn>,
            inplace_stop_token
        >;

} // namespace cocoro::details

namespace cocoro::env {

    inline constexpr details::get_stop_token_query_fn get_stop_token{};

    class inplace_stop_env : private details::pinned
    {
        enum class type : unsigned char { source, token, none };
    public:
        inplace_stop_env() noexcept
            : stop_type(type::source), source{}
        {}

        template<details::inplace_stop_compatible OtherEnv>
        inplace_stop_env(inherit_tag, const OtherEnv& other) noexcept
            : stop_type(type::token), token{ get_stop_token(other) }
        {}

        inplace_stop_env(inherit_tag, const auto&) noexcept
            : stop_type(type::none), unused{}
        {}

        constexpr bool is_source() const noexcept {
            return stop_type == type::source;
        }

        constexpr bool is_token() const noexcept {
            return stop_type == type::token || stop_type == type::none;
        }

        inplace_stop_token get_token() const noexcept {
            switch (stop_type) {
            case type::source:
                return source.get_token();
            case type::token:
                return token;
            case type::none:
            default:
                return inplace_stop_token{};
            }
        }

        bool stop_possible() const noexcept {
            switch (stop_type) {
            case type::source:
                return source.stop_possible();
            case type::token:
                return token.stop_possible();
            case type::none:
            default:
                return false;
            }
        }

        bool stop_requested() const noexcept {
            switch (stop_type) {
            case type::source:
                return source.stop_requested();
            case type::token:
                return token.stop_requested();
            case type::none:
            default:
                return false;
            }
        }

        bool requrest_stop() noexcept {
            switch (stop_type) {
            case type::source:
                return source.request_stop();
            case type::token:
            case type::none:
            default:
                return false;
            }
        }

        inplace_stop_token query(decltype(get_stop_token)) const noexcept {
            return get_token();
        }

    private:
        union {
            inplace_stop_source source;
            inplace_stop_token token;
            never_stop_token unused;
        };
        type stop_type = type::none;
    };

} // namespace cocoro::env

#endif // COCORO_ENVIRONMENT_CANCELLATION_H
