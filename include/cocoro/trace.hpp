#pragma once
#ifndef COCORO_COROTRACE_H
#define COCORO_COROTRACE_H 1

#include <source_location>
#include <utility>
#include <coroutine>
#include <vector>
#include <string>
#include <format>
#include <algorithm>

// #include "cocoro/utils.hpp"

namespace cocoro {

    struct inplace_trace_entry {
        const inplace_trace_entry* prev = nullptr;
        std::source_location loc = {};
    };

    template<typename Promise>
    concept traceable_promise = requires (Promise& promise) {
        { promise.trace_entry() } noexcept -> std::same_as<const inplace_trace_entry&>;
    };

    class corotrace_entry
    {
    public:
        corotrace_entry() = default;
        corotrace_entry(const corotrace_entry&) = default;
        corotrace_entry& operator=(const corotrace_entry&) = default;
        corotrace_entry(corotrace_entry&&) = default;
        corotrace_entry& operator=(corotrace_entry&&) = default;

        explicit corotrace_entry(std::source_location loc) :
            function_name(loc.function_name()),
            file_name(loc.file_name()),
            line(loc.line()),
            column(loc.column())
        {}

        const std::string& coroutine_name() const noexcept { return function_name; }
        const std::string& source_file() const noexcept { return file_name; }
        std::uint_least32_t source_line() const noexcept { return line; }
        std::uint_least32_t source_column() const noexcept { return column; }

        std::string description() const;

    private:
        std::string function_name;
        std::string file_name;
        std::uint_least32_t line = 0;
        std::uint_least32_t column = 0;
    };

    class corotrace
    {
    public:
        corotrace() = default;
        corotrace(const corotrace&) = default;
        corotrace& operator=(const corotrace&) = default;
        corotrace(corotrace&&) = default;
        corotrace& operator=(corotrace&&) = default;

        class [[nodiscard]] current_trace_awaiter
        {
        public:
            constexpr bool await_ready() const noexcept { return false; }

            template<traceable_promise Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
                entry = &handle.promise().trace_entry();
                return false; // resume immediately
            }

            corotrace await_resume() const { return corotrace(entry); }

        private:
            const inplace_trace_entry* entry = nullptr;
        };

        // co_await the result of this function to get the current corotrace
        static current_trace_awaiter current() noexcept { return {}; }

        auto begin() const noexcept { return entries.cbegin(); }
        auto end() const noexcept { return entries.cend(); }
        bool empty() const noexcept { return entries.empty(); }
        std::size_t size() const noexcept { return entries.size(); }

    private:
        friend current_trace_awaiter;
        explicit corotrace(const inplace_trace_entry* entry) {
            while (entry != nullptr) {
                entries.emplace_back(entry->loc);
                entry = entry->prev;
            }
        }

        std::vector<corotrace_entry> entries;
    };

    class trace_querier
    {
    public:
        using srcloc = std::source_location;

        trace_querier() = default;

        void set_suspension_point_info(srcloc&& loc) noexcept {
            this->entry.loc = std::move(loc);
        }

        // Transparent await_transform, but records source location info.
        // If derived class provides other await_transform overloads, they should
        // also record source location info.
        template<typename T>
        T&& await_transform(T&& awaitable, srcloc loc = srcloc::current()) noexcept {
            set_suspension_point_info(std::move(loc));
            return std::forward<T>(awaitable);
        }

        const srcloc& suspension_point_info() const noexcept { return entry.loc; }

        template<typename OtherPromise>
        void query_promise(OtherPromise& promise) noexcept {
            if constexpr (traceable_promise<OtherPromise>) {
                entry.prev = &promise.trace_entry();
            } else {
                entry.prev = nullptr;
            }
        }

        const inplace_trace_entry& trace_entry() const noexcept { return entry; }

    protected:
        inplace_trace_entry entry = {};
    };

} // namespace cocoro

// formatter for corotrace_entry and corotrace
namespace std {

    template<>
    struct formatter<cocoro::corotrace_entry> {
        static constexpr std::size_t enable_full_name = std::numeric_limits<std::size_t>::max();
        std::size_t function_name_max_width = enable_full_name;

        constexpr auto parse(format_parse_context& ctx) {
            auto it = ctx.begin();
            const auto end = ctx.end();
            if (it == end || *it == '}') {
                return it;
            }
            function_name_max_width = 0;
            while (it != end && *it != '}') {
                const char ch = *it;
                if (ch >= '0' && ch <= '9') {
                    function_name_max_width = function_name_max_width * 10 + (ch - '0');
                } else {
                    throw format_error("invalid format for corotrace_entry");
                }
                ++it;
            }
            if (function_name_max_width < 4) {
                throw format_error("width must be at least 4");
            } 
            return it;
        }

        template<typename FormatContext>
        auto format(const cocoro::corotrace_entry& entry, FormatContext& ctx) const {
            if (function_name_max_width == enable_full_name) {
                return format_to(ctx.out(), "{} at {}:{}:{}",
                    entry.coroutine_name(),
                    entry.source_file(),
                    entry.source_line(),
                    entry.source_column()
                );
            }

            auto out = ctx.out();
            const std::string_view name(entry.coroutine_name());

            if (name.length() <= function_name_max_width) {
                out = format_to(out, "{}", name);
                std::ranges::fill_n(out, function_name_max_width - name.length(), ' ');
            } else {
                out = format_to(out, "{}...", name.substr(0, function_name_max_width - 3));
            }

            return format_to(out, " at {}:{}:{}",
                entry.source_file(),
                entry.source_line(),
                entry.source_column()
            );
        }
    };

    template<>
    struct formatter<cocoro::corotrace> {
        formatter<cocoro::corotrace_entry> entry_formatter;

        constexpr auto parse(format_parse_context& ctx) {
            return entry_formatter.parse(ctx);
        }

        template<typename FormatContext>
        auto format(const cocoro::corotrace& trace, FormatContext& ctx) const {
            auto out = ctx.out();
            const auto size = trace.size();
            for (std::size_t count = 0; const cocoro::corotrace_entry& entry : trace) {
                out = format_to(out, "#{} ", count++);
                out = entry_formatter.format(entry, ctx);
                if (count < size) {
                    *out = '\n';
                    ++out;
                }
            }
            return out;
        }
    };

} // namespace std

inline std::string cocoro::corotrace_entry::description() const {
    return std::format("{}", *this);
}

#endif // COCORO_COROTRACE_H
