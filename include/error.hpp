#ifndef ERROR_HPP
#define ERROR_HPP

#include <format>
#include <variant>
#include "magic_enum/magic_enum.hpp"
#include "types.hpp"

enum class LexicalError : u8 {
    EmptyQuotedIden,
    UnexpectedEof,
    UnknownEscape,
    UnknownToken,
};

enum class ParseError : u8 {
    UnexpectedToken,
};

using CompileError = std::variant<LexicalError, ParseError>;

template<>
struct std::formatter<CompileError, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(CompileError err, std::format_context& ctx) {
        return std::visit(
            [&](auto&& value) {
                return std::format_to(ctx.out(), "{}", magic_enum::enum_name(value));
            },
            err);
    }
};

#endif
