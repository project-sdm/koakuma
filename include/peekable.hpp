#ifndef PEEKABLE_HPP
#define PEEKABLE_HPP

#include "lexer.hpp"
#include "token.hpp"
#include <expected>
#include <optional>
#include <string>

class Peekable
{
public:

  explicit Peekable(std::string);

  std::expected<Token, CompileError> peek();
  std::expected<Token, CompileError> next();

private:

  Lexer lexer;
  std::optional<Token> buf = std::nullopt;
};

#endif
