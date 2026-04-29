#ifndef UTIL_HPP
#define UTIL_HPP

#include <array>
#include <bit>
#include <cassert>
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
