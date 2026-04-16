#ifndef UTIL_HPP
#define UTIL_HPP

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

#endif
