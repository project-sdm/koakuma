#ifndef PARSER_HPP
#define PARSER_HPP

#include "ast.hpp"
#include "error.hpp"
#include "peekable.hpp"
#include <expected>
#include <vector>

class Parser
{
public:

  std::expected<SourceFile, std::vector<CompileError>> source_file();

  Parser(std::string source);

private:

  template <auto>
  bool accept();

  template <auto>
  std::optional<CompileError> expect();

  std::expected<Statement, CompileError> statement();

  std::expected<CreateStatement, CompileError> create_statement();
  std::expected<SelectStatement, CompileError> select_statement();
  std::expected<InsertStatement, CompileError> insert_statement();
  std::expected<DeleteStatement, CompileError> delete_statement();

  std::expected<InsertValue, CompileError> insert_value();
  std::expected<Filter, CompileError> where_declaration();

  Peekable tokens;
};

template <auto value>
bool Parser::accept()
{
  using T = decltype(value);

  auto t = tokens.peek();
  if (!t.has_value())
    return false;

  auto *tok = t->get_if<T>();
  if (tok && *tok == value) {
    tokens.next();
    return true;
  }

  return false;
}

template <auto value>
std::optional<CompileError> Parser::expect()
{
  using T = decltype(value);

  auto t = tokens.peek();
  if (!t.has_value())
    return t.error();

  auto *tok = t->get_if<T>();
  if (tok && *tok == value) {
    tokens.next();
    return std::nullopt;
  }

  return ParseError::UnexpectedToken;
}

#endif
