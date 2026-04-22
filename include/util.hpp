#ifndef UTIL_HPP
#define UTIL_HPP

#include <cstddef>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <type_traits>
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
    template<typename T>
        requires std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>
    T span_read(std::span<const u8> data, std::size_t offset) {
        T val;
        std::memcpy(&val, data.subspan(offset).data(), sizeof(val));
        return val;
    }

    template<typename T>
        requires std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>
    void span_write(std::span<u8> data, std::size_t offset, const T& value) {
        std::memcpy(data.subspan(offset).data(), &value, sizeof(value));
    }

}  // namespace util

#endif
