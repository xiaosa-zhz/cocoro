#pragma once
#ifndef COCORO_UTILITYS_SYMMETRIC_RESULT_H
#define COCORO_UTILITYS_SYMMETRIC_RESULT_H 1

#include <concepts>
#include <exception>

#include "./basic.hpp"

namespace cocoro::details {

    template<typename Derived>
    class symmetric_result_base : private pinned
    {
    public:
        enum class status : unsigned char {
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

} // namespace cocoro::details

namespace cocoro {

    template<typename Value>
    class symmetric_result : public details::symmetric_result_base<symmetric_result<Value>>
    {
    public:
        using value_type = Value;
    private:
        using base = details::symmetric_result_base<symmetric_result<value_type>>;
        static constexpr bool reference_result = std::is_reference_v<value_type>;
        friend base;
    public:
        using status = base::status;
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
            details::monostate dummy;
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
            details::monostate dummy;
            std::exception_ptr exception;

            storage_type() noexcept : dummy{} {}
            ~storage_type() {}
        };
        storage_type storage;
        status state = status::uninitilized;
    };

} // namespace cocoro

#endif // COCORO_UTILITYS_SYMMETRIC_RESULT_H
