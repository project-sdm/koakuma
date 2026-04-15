#ifndef LEXER_HPP
#define LEXER_HPP

#include "error.hpp"
#include "token.hpp"
#include <expected>
#include <optional>
#include <string>

class Lexer
{
public:

  explicit Lexer(std::string);

  std::expected<Token, CompileError> next();

private:

  std::optional<char> peek();
  void consume();

  std::string source;
  std::size_t pos = 0;
};

#endif
