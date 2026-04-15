#include "error.hpp"
#include "lexer.hpp"
#include "peekable.hpp"
#include "token.hpp"
#include <expected>
#include <string>
#include <utility>

Peekable::Peekable(std::string source)
: lexer{std::move(source)}
{}

std::expected<Token, CompileError> Peekable::peek()
{
  if (buf.has_value())
    return *buf;

  auto res = lexer.next();
  if (res.has_value())
    buf = *res;

  return res;
}

std::expected<Token, CompileError> Peekable::next()
{
  if (!buf.has_value())
    return lexer.next();

  auto tok = *buf;
  buf.reset();

  return tok;
}
