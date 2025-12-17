#pragma once
#ifndef COCORO_COROUTILS_H
#define COCORO_COROUTILS_H 1

#include <concepts>
#include <type_traits>
#include <cstdint>
#include <utility>
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

    template<typename Derived>
    class symmetric_result_base : private pinned
    {
    public:
        enum class status : std::uint8_t {
            uninitilized, value, exception,
        };

        symmetric_result_base() = default;

        void unhandled_exception() noexcept {
            auto& self = this->self();
            self.reset();
            self.storage.exception = std::current_exception();
            self.state = status::exception;
        }

        void throw_if_exception() const {
            const auto& self = this->self();
            if (self.state == status::exception) {
                std::rethrow_exception(self.storage.exception);
            }
        }

        status state() const noexcept {
            return this->self().state;
        }

    private:
        Derived& self() noexcept {
            return static_cast<Derived&>(*this);
        }

        const Derived& self() const noexcept {
            return static_cast<const Derived&>(*this);
        }
    };

    struct dummy_t {};

} // namespace cocoro::details

namespace cocoro {

    template<typename Value>
    class symmetric_result : public details::symmetric_result_base<symmetric_result<Value>>
    {
        using base = details::symmetric_result_base<symmetric_result<Value>>;
        static constexpr bool reference_result = std::is_reference_v<Value>;
        friend base;
    public:
        using status = base::status;
        using value_type = Value;
        using data_type = std::conditional_t<reference_result,
            std::add_pointer_t<value_type>,
            value_type>;

        symmetric_result() = default;

        ~symmetric_result() {
            this->reset();  
        }

        void reset() noexcept {
            if (this->state == status::value) {
                this->storage.value.~data_type();
            } else if (this->state == status::exception) {
                storage.exception.~exception_ptr();
            }
            this->state = status::uninitilized;
        }

        void return_value(value_type rt) noexcept requires reference_result {
            this->reset();
            this->storage.value = std::addressof(rt);
            this->state = status::value;
        }

        template<typename U = value_type>
            requires (not reference_result)
            && std::convertible_to<U, value_type>
            && std::constructible_from<value_type, U>
        void return_value(U&& rt) noexcept(std::is_nothrow_constructible_v<value_type, U>) {
            this->reset();
            new (std::addressof(this->storage.value)) value_type(std::forward<U>(rt));
            this->state = status::value;
        }

        value_type result() {
            this->throw_if_exception();
            if constexpr (reference_result) {
                return static_cast<value_type>(*this->storage.value);
            } else {
                return std::move(this->storage.value);
            }
        }

    private:
        union storage_type {
            details::dummy_t dummy;
            data_type value;
            std::exception_ptr exception;

            storage_type() noexcept : dummy{} {}
            ~storage_type() {}
        };
        storage_type storage;
        status state = status::uninitilized;
    };

    template<typename Void>
        requires std::is_void_v<Void>
    class symmetric_result<Void> : public details::symmetric_result_base<symmetric_result<Void>>
    {
        using base = details::symmetric_result_base<symmetric_result<Void>>;
        friend base;
    public:
        using status = base::status;
        using value_type = void;

        symmetric_result() = default;

        ~symmetric_result() {
            this->reset();
        }

        void reset() noexcept {
            if (this->state == status::exception) {
                storage.exception.~exception_ptr();
            }
            this->state = status::uninitilized;
        }

        void return_void() noexcept {
            this->reset();
            this->state = status::value;
        }

        void result() {
            this->throw_if_exception();
        }

    private:
        union storage_type {
            details::dummy_t dummy;
            std::exception_ptr exception;

            storage_type() noexcept : dummy{} {}
            ~storage_type() {}
        };
        storage_type storage;
        status state = status::uninitilized;
    };

    template<typename Querier, typename Promise>
    concept promise_querier = requires (Querier& querier, Promise& promise) {
        querier.query_promise(promise);
    };

    template<typename Querier, typename Promise>
    concept nothrow_promise_querier = requires (Querier& querier, Promise& promise) {
        { querier.query_promise(promise) } noexcept;
    };

    template<typename... Queriers>
    struct composed_promise_queriers : Queriers... {
        template<typename Promise>
            requires (promise_querier<Queriers, Promise> && ...)
        void query_promise(Promise& promise)
            noexcept((nothrow_promise_querier<Queriers, Promise> && ...)) {
            (Queriers::query_promise(promise), ...);
        }
    };

    template<typename Promise>
    concept stoppable_promise = requires (Promise& promise) {
        { promise.unhandled_stopped() } noexcept -> std::convertible_to<std::coroutine_handle<>>;
    };

    class stop_querier
    {
    public:
        using stopped_handler_type = std::coroutine_handle<>(*)(void*);

        stop_querier() = default;

        template<typename OtherPromise>
        static std::coroutine_handle<> default_stopped_handler(void* addr) noexcept {
            return std::coroutine_handle<OtherPromise>::from_address(addr)
                .promise().unhandled_stopped();
        }

        [[noreturn]]
        static std::coroutine_handle<> terminate_stopped_handler(void*) noexcept {
            std::terminate();
        }

        template<typename OtherPromise>
        void query_promise(OtherPromise&) noexcept {
            if constexpr (stoppable_promise<OtherPromise>) {
                stopped_handler = &default_stopped_handler<OtherPromise>;
            } else {
                stopped_handler = &terminate_stopped_handler;
            }
        }

        template<typename Self>
        std::coroutine_handle<> unhandled_stopped(this Self& self) noexcept {
            return self.stopped_handler(self.continuation().address());
        }

    private:
        stopped_handler_type stopped_handler = &terminate_stopped_handler;
    };

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

    template<std::default_initializable... Queriers>
    class basic_promise_base :
        public composed_promise_queriers<Queriers...>,
        private details::pinned
    {
        using querier_base = composed_promise_queriers<Queriers...>;
    public:
        basic_promise_base() = default;

        // If more erased operations are needed, derived classes should write their own
        // set_continuation and call this base implementation.
        template<typename OtherPromise>
            requires (not std::same_as<OtherPromise, void>)
        void set_continuation(std::coroutine_handle<OtherPromise> handle)
            noexcept(nothrow_promise_querier<querier_base, OtherPromise>) {
            querier_base::query_promise(handle.promise());
            cont = handle;
        }

        std::coroutine_handle<> continuation() const noexcept { return cont; }

        std::suspend_always initial_suspend() noexcept { return {}; }

        continue_final_awaiter final_suspend() noexcept { return {}; }

    protected:
        std::coroutine_handle<> cont = nullptr;
    };

} // namespace cocoro

#endif // COCORO_COROUTILS_H
