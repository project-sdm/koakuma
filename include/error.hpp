#ifndef ERROR_HPP
#define ERROR_HPP

#include "magic_enum/magic_enum.hpp"
#include <format>
#include <variant>

enum class LexicalError
{
  EmptyQuotedIden,
  UnexpectedEof,
  UnknownEscape,
  UnknownToken,
};

enum class ParseError
{
  UnexpectedToken,
};

using CompileError = std::variant<LexicalError, ParseError>;

template <>
struct std::formatter<CompileError, char>
{

  constexpr auto parse(std::format_parse_context& ctx)
  {
    return ctx.begin();
  }

  auto format(CompileError err, std::format_context& ctx) const
  {
    return std::visit([&](auto&& value) {
      return std::format_to(ctx.out(), "{}", magic_enum::enum_name(value));
    }, err);
  }
  
};

#endif
