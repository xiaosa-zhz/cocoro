#pragma once
#ifndef COCORO_ENVIRONMENT_BASIC_H
#define COCORO_ENVIRONMENT_BASIC_H 1

#include <concepts>
#include <type_traits>
#include <utility>

namespace cocoro::env {

    template<typename State>
    concept env_aware = requires (const State& state) {
        { state.get_env() } noexcept;
    };

    template<typename State>
    struct env_traits {};

    template<env_aware State>
    struct env_traits<State> {
        using type = decltype(std::declval<const State&>().get_env());
    };

    template<env_aware State>
        requires requires { typename State::env_type; }
    struct env_traits<State> {
        using type = State::env_type;
    };

    template<typename State>
    using env_t = env_traits<State>::type;

    struct inherit_tag {};
    inline constexpr inherit_tag inherit{}; 

} // namespace cocoro::env

namespace cocoro::details {

    struct get_env_fn {
        template<env::env_aware State>
        constexpr const env::env_t<State>& operator()(const State& env) const noexcept {
            return env.get_env();
        }
    };

} // namespace cocoro::details

namespace cocoro::env {

    inline constexpr details::get_env_fn get_env{};

    template<typename Env, typename Query>
    concept queryable = requires (const Env& env, Query q) {
        { env.query(q) } noexcept;
    };

    template<typename Env, typename Query, typename Result>
    concept queryable_r = requires (const Env& env, Query q) {
        { env.query(q) } noexcept -> std::convertible_to<Result>;
    };

    template<typename Query, typename Env>
    concept has_query = queryable<Env, Query>;

    template<typename Env, typename Query>
    using query_result_t = decltype(std::declval<const Env&>().query(std::declval<Query>()));

    // Environment inherit configs from other environments by copy-construct from them.
    // An inheritable environment should at least be inheritable from itself.
    // Typical inherit initialization is completed by do some queries on the other environment
    // and use the result of queries to initialize itself.
    // If source environment is not suitable for inheriting (e.g. no required queries),
    // the inheritable environment may use default constructor to initialize itself.
    // If environment is not default constructible in this case, error should be reported.
    template<typename Env>
    concept inheritable = std::constructible_from<Env, inherit_tag, const Env&>
        && std::is_nothrow_constructible_v<Env, inherit_tag, const Env&>;

    template<inheritable... Envs>
    struct composed_environment : Envs... {
        using Envs::query...;

        composed_environment() = default;

        template<typename OtherEnv>
        composed_environment(inherit_tag, const OtherEnv& other) noexcept
            : Envs(inherit, other)...
        {}
    };

} // namespace cocoro

#endif // COCORO_ENVIRONMENT_BASIC_H
