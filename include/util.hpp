#ifndef UTIL_HPP
#define UTIL_HPP

#include <array>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include "types.hpp"

// https://code-examples.net/en/q/4bca16c/how-to-propagate-std-expected-errors-without-boilerplate
//
// Permite propagar el error de un std::expected. Usa una extensión de GCC para tener expresiones
// con statements dentro.
#define TRY(expr)                                     \
    ({                                                \
        auto&& __result = (expr);                     \
        if (!__result)                                \
            return std::unexpected(__result.error()); \
                                                      \
        std::move(*__result);                         \
    })

#define TRYV(expr)                                    \
    ({                                                \
        auto&& __result = (expr);                     \
        if (!__result)                                \
            return std::unexpected(__result.error()); \
    })

#define TRY_OPT(expr)            \
    ({                           \
        auto&& __opt = (expr);   \
        if (!__opt)              \
            return std::nullopt; \
                                 \
        std::move(*__opt);       \
    })

namespace util {
    std::string getenv_or(const char* name, std::string_view default_value);

    template<typename T>
    concept iter = requires(T it) {
        typename T::value_type;
        { it.next() } -> std::same_as<std::optional<typename T::value_type>>;
    };

    template<iter Iter>
    class Peekable {
    private:
        using value_type = typename Iter::value_type;

        Iter it;
        std::optional<value_type> buf = std::nullopt;

    public:
        template<typename... Args>
        explicit Peekable(Args&&... args)
            : it{std::forward<Args>(args)...} {}

        std::optional<value_type> next() {
            if (buf)
                return *std::exchange(buf, std::nullopt);

            return it.next();
        }

        std::optional<std::reference_wrapper<value_type>> peek() {
            if (!buf)
                buf = it.next();

            if (!buf)
                return std::nullopt;

            return *buf;
        }
    };

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    T span_read(std::span<const u8> data, std::size_t offset) {
        auto sub = data.subspan(offset);
        assert(sub.size() >= sizeof(T));

        std::array<u8, sizeof(T)> buf;
        std::memcpy(buf.data(), sub.data(), sizeof(T));
        return std::bit_cast<T>(buf);
    }

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    void span_write(std::span<u8> data, std::size_t offset, const T& value) {
        auto sub = data.subspan(offset);
        assert(sub.size() >= sizeof(value));

        std::memcpy(sub.data(), &value, sizeof(value));
    }

}  // namespace util

#endif
