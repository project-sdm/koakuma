#include "parser.hpp"

Parser::Parser(std::string source)
: tokens{std::move(source)}
{}

std::expected<SourceFile, std::vector<CompileError>> Parser::source_file()
{
  std::vector<Statement> statements;
  std::vector<CompileError> errors;

  while (true) {
    auto res = tokens.peek();
    if (!res.has_value()) {
      errors.emplace_back(res.error());
      break;
    }

    if (res->is<Eof>())
      break;

    if (auto stmt = statement())
      statements.emplace_back(*stmt);
    else {
      tokens.next();
      auto err = stmt.error();
      errors.emplace_back(err);

      if (std::holds_alternative<LexicalError>(err))
        break;
    }
  }

  if (!errors.empty())
    return std::unexpected{errors};
  else
    return SourceFile{statements};
}

std::expected<Statement, CompileError> Parser::statement()
{
  auto res = tokens.peek();
  if (!res.has_value())
    return std::unexpected{res.error()};

  if (auto *tok = res->get_if<Keyword>()) {
    switch (*tok) {
    case Keyword::Create: return create_statement();
    case Keyword::Select: return select_statement();
    case Keyword::Insert: return insert_statement();
    case Keyword::Delete: return delete_statement();
    default: return std::unexpected{ParseError::UnexpectedToken};
    }
  }

  return std::unexpected{ParseError::UnexpectedToken};
}

std::expected<CreateStatement, CompileError> Parser::create_statement()
{
  CreateStatement stmt;

  if (auto opt = expect<Keyword::Create>())
    return std::unexpected{opt.value()};

  if (auto opt = expect<Keyword::Table>())
    return std::unexpected{opt.value()};

  auto res = tokens.next();
  if (!res.has_value())
    return std::unexpected{res.error()};

  if (auto *tok = res->get_if<Identifier>())
    stmt.table_name = tok->value;
  else if (auto *tok = res->get_if<QuotedIdentifier>())
    stmt.table_name = tok->value;
  else
    return std::unexpected{ParseError::UnexpectedToken};

  if (auto opt = expect<Symbol::LParen>())
    return std::unexpected{opt.value()};

  while (true) {
    Column col;

    auto res = tokens.next();
    if (!res.has_value())
      return std::unexpected{res.error()};

    if (auto *tok = res->get_if<Identifier>())
      col.name = tok->value;
    else if (auto *tok = res->get_if<QuotedIdentifier>())
      col.name = tok->value;
    else
      return std::unexpected{ParseError::UnexpectedToken};

    res = tokens.next();
    if (!res.has_value())
      return std::unexpected{res.error()};

    if (auto *tok = res->get_if<DataType>())
      col.type = *tok;
    else
      return std::unexpected{ParseError::UnexpectedToken};

    if (accept<Keyword::Index>()) {
      auto res = tokens.next();
      if (!res.has_value())
        return std::unexpected{res.error()};

      if (auto *tok = res->get_if<Identifier>())
        col.index_name = tok->value;
      else if (auto *tok = res->get_if<QuotedIdentifier>())
        col.index_name = tok->value;
      else
        return std::unexpected{ParseError::UnexpectedToken};
    }

    stmt.columns.emplace_back(std::move(col));

    if (!accept<Symbol::Comma>())
      break;
  }

  if (auto opt = expect<Symbol::RParen>())
    return std::unexpected{opt.value()};

  if (accept<Keyword::From>()) {
    if (auto opt = expect<Keyword::File>())
      return std::unexpected{opt.value()};

    auto res = tokens.next();
    if (!res.has_value())
      return std::unexpected{res.error()};

    if (auto *tok = res->get_if<Literal>())
      stmt.file_path = tok->value;
    else
      return std::unexpected{ParseError::UnexpectedToken};
  }

  if (auto opt = expect<Symbol::SemiColon>())
    return std::unexpected{opt.value()};

  return stmt;
}

std::expected<SelectStatement, CompileError> Parser::select_statement()
{
  SelectStatement stmt;

  if (auto opt = expect<Keyword::Select>())
    return std::unexpected{opt.value()};

  if (auto opt = expect<Symbol::Asterisk>())
    return std::unexpected{opt.value()};

  if (auto opt = expect<Keyword::From>())
    return std::unexpected{opt.value()};

  auto res = tokens.next();
  if (!res.has_value())
    return std::unexpected{res.error()};

  if (auto *tok = res->get_if<Identifier>())
    stmt.table_name = tok->value;
  else if (auto *tok = res->get_if<QuotedIdentifier>())
    stmt.table_name = tok->value;
  else
    return std::unexpected{ParseError::UnexpectedToken};

  if (accept<Keyword::Where>()) {
    auto filter = where_declaration();
    if (!filter.has_value())
      return std::unexpected{filter.error()};

    stmt.filter = std::move(*filter);
  }

  if (auto opt = expect<Symbol::SemiColon>())
    return std::unexpected{opt.value()};

  return stmt;
}

std::expected<InsertStatement, CompileError> Parser::insert_statement()
{
  InsertStatement stmt;

  if (auto opt = expect<Keyword::Insert>())
    return std::unexpected{opt.value()};

  if (auto opt = expect<Keyword::Into>())
    return std::unexpected{opt.value()};

  auto res = tokens.next();
  if (!res.has_value())
    return std::unexpected{res.error()};

  if (auto *tok = res->get_if<Identifier>())
    stmt.table_name = tok->value;
  else if (auto *tok = res->get_if<QuotedIdentifier>())
    stmt.table_name = tok->value;
  else
    return std::unexpected{ParseError::UnexpectedToken};

  if (auto opt = expect<Keyword::Values>())
    return std::unexpected{opt.value()};

  if (auto opt = expect<Symbol::LParen>())
    return std::unexpected{opt.value()};

  auto insert_val = insert_value();
  if (!insert_val.has_value())
    return std::unexpected{insert_val.error()};

  if (auto opt = expect<Symbol::RParen>())
    return std::unexpected{opt.value()};

  stmt.values.emplace_back(std::move(*insert_val));

  while (accept<Symbol::LParen>()) {
    auto insert_val = insert_value();
    if (!insert_val.has_value())
      return std::unexpected{insert_val.error()};

    if (auto opt = expect<Symbol::RParen>())
      return std::unexpected{opt.value()};

    stmt.values.emplace_back(std::move(*insert_val));
  }

  if (auto opt = expect<Symbol::SemiColon>())
    return std::unexpected{opt.value()};

  return stmt;
}

std::expected<DeleteStatement, CompileError> Parser::delete_statement()
{
  DeleteStatement stmt;

  if (auto opt = expect<Keyword::Delete>())
    return std::unexpected{opt.value()};

  if (auto opt = expect<Keyword::From>())
    return std::unexpected{opt.value()};

  auto res = tokens.next();
  if (!res.has_value())
    return std::unexpected{res.error()};

  if (auto *tok = res->get_if<Identifier>())
    stmt.table_name = tok->value;
  else if (auto *tok = res->get_if<QuotedIdentifier>())
    stmt.table_name = tok->value;
  else
    return std::unexpected{ParseError::UnexpectedToken};

  if (auto opt = expect<Keyword::Where>())
    return std::unexpected{opt.value()};

  auto filter = where_declaration();
  if (!filter.has_value())
    return std::unexpected{filter.error()};
  stmt.filter = std::move(*filter);

  if (auto opt = expect<Symbol::SemiColon>())
    return std::unexpected{opt.value()};

  return stmt;
}


std::expected<InsertValue, CompileError> Parser::insert_value()
{
  InsertValue value;

  auto res = tokens.next();
  if (!res.has_value())
    return std::unexpected{res.error()};

  if (auto *tok = res->get_if<Literal>())
    value.exprs.emplace_back(*tok);
  else if (auto *tok = res->get_if<Number>())
    value.exprs.emplace_back(*tok);
  else
    return std::unexpected{ParseError::UnexpectedToken};

  while (accept<Symbol::Comma>()) {
    auto res = tokens.next();
    if (!res.has_value())
      return std::unexpected{res.error()};

    if (auto *tok = res->get_if<Literal>())
      value.exprs.emplace_back(*tok);
    else if (auto *tok = res->get_if<Number>())
      value.exprs.emplace_back(*tok);
    else
      return std::unexpected{ParseError::UnexpectedToken};
  }

  return value;
}

std::expected<Filter, CompileError> Parser::where_declaration()
{
  Filter filter;

  auto res = tokens.next();
  if (!res.has_value())
    return std::unexpected{res.error()};

  if (auto *tok = res->get_if<Identifier>())
    filter.col_identifier = tok->value;
  else if (auto *tok = res->get_if<QuotedIdentifier>())
    filter.col_identifier = tok->value;
  else
    return std::unexpected{ParseError::UnexpectedToken};

  if (accept<Symbol::Eq>()) {
    EqFilter eqfilter;

    auto res = tokens.next();
    if (!res.has_value())
      return std::unexpected{res.error()};

    if (auto *tok = res->get_if<Literal>())
      eqfilter.value = std::move(tok->value);
    else if (auto *tok = res->get_if<Number>())
      eqfilter.value = tok->value;
    else
      return std::unexpected{ParseError::UnexpectedToken};

    filter.data = std::move(eqfilter);

  } else if (accept<Keyword::Between>()) {
    RangeFilter rangefilter;

    auto res = tokens.next();
    if (!res.has_value())
      return std::unexpected{res.error()};

    if (auto *tok = res->get_if<Number>())
      rangefilter.min_val = tok->value;
    else
      return std::unexpected{ParseError::UnexpectedToken};

    if (auto opt = expect<Keyword::And>())
      return std::unexpected{opt.value()};

    res = tokens.next();
    if (!res.has_value())
      return std::unexpected{res.error()};

    if (auto *tok = res->get_if<Number>())
      rangefilter.max_val = tok->value;
    else
      return std::unexpected{ParseError::UnexpectedToken};

    filter.data = std::move(rangefilter);

  } else if (accept<Keyword::In>()) {
    Point2D origin;

    if (auto opt = expect<Symbol::LParen>())
      return std::unexpected{opt.value()};

    if (auto opt = expect<Keyword::Point>())
      return std::unexpected{opt.value()};

    if (auto opt = expect<Symbol::LParen>())
      return std::unexpected{opt.value()};

    auto res = tokens.next();
    if (!res.has_value())
      return std::unexpected{res.error()};

    // x
    if (auto *tok = res->get_if<Number>())
      origin.x = tok->value;
    else
      return std::unexpected{ParseError::UnexpectedToken};

    if (auto opt = expect<Symbol::Comma>())
      return std::unexpected{opt.value()};

    res = tokens.next();
    if (!res.has_value())
      return std::unexpected{res.error()};

    // y
    if (auto *tok = res->get_if<Number>())
      origin.y = tok->value;
    else
      return std::unexpected{ParseError::UnexpectedToken};

    if (auto opt = expect<Symbol::RParen>())
      return std::unexpected{opt.value()};

    if (auto opt = expect<Symbol::Comma>())
      return std::unexpected{opt.value()};

    if (accept<Keyword::Radius>()) {
      auto res = tokens.next();
      if (!res.has_value())
        return std::unexpected{res.error()};

      if (auto *tok = res->get_if<Number>())
        filter.data = RadFilter{origin, tok->value};
      else
        return std::unexpected{ParseError::UnexpectedToken};
    }
    else if (accept<Keyword::K>()) {
      auto res = tokens.next();
      if (!res.has_value())
        return std::unexpected{res.error()};

      if (auto *tok = res->get_if<Number>())
        filter.data = KFilter{origin, tok->value};
      else
        return std::unexpected{ParseError::UnexpectedToken};
    } else return std::unexpected{ParseError::UnexpectedToken};

    if (auto opt = expect<Symbol::RParen>())
      return std::unexpected{opt.value()};
  } else return std::unexpected{ParseError::UnexpectedToken};

  return filter;
}
