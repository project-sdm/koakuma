#ifndef AST_HPP
#define AST_HPP

#include "token.hpp"
#include <memory>
#include <variant>
#include <string>
#include <vector>
#include <format>

struct Column
{
  std::string name;
  DataType type;
  std::optional<std::string> index_name;
};

struct EqFilter
{
  std::variant<std::string, f64> value;
};

struct RangeFilter
{
  f64 min_val;
  f64 max_val;
};

struct Point2D
{
  f64 x;
  f64 y;
};

struct RadFilter
{
  Point2D origin;
  f64 radius;
};

struct KFilter
{
  Point2D origin;
  f64 k;
};

using FilterData = std::variant<EqFilter, RangeFilter, RadFilter, KFilter>;

struct Filter
{
  std::string col_identifier;
  FilterData data;
};

using Expr = std::variant<Literal, Number>;

struct InsertValue
{
  std::vector<Expr> exprs;
};

struct CreateStatement
{
  std::string table_name;
  std::vector<Column> columns;
  std::optional<std::string> file_path = std::nullopt;
};

struct SelectStatement
{
  std::string table_name;
  std::optional<Filter> filter = std::nullopt;
};

struct InsertStatement
{
  std::string table_name;
  std::vector<InsertValue> values;
};

struct DeleteStatement
{
  std::string table_name;
  Filter filter;
};

using Statement = std::variant<CreateStatement, SelectStatement, InsertStatement, DeleteStatement>;

struct SourceFile
{
  std::vector<Statement> statements;
};

template <>
struct std::formatter<Point2D, char>
{

  constexpr auto parse(std::format_parse_context& ctx)
  {
    return ctx.begin();
  }

  auto format(Point2D point, std::format_context& ctx) const
  {
    return std::format_to(ctx.out(), "Point2D({}, {})", point.x, point.y);
  }

};

template <>
struct std::formatter<EqFilter, char>
{

  constexpr auto parse(std::format_parse_context& ctx)
  {
    return ctx.begin();
  }

  auto format(EqFilter filter, std::format_context& ctx) const
  {
    return std::visit([&](auto&& value) {
      return std::format_to(ctx.out(), "EqFilter: {}", value);
    }, filter.value);
  }

};

template <>
struct std::formatter<RangeFilter, char>
{

  constexpr auto parse(std::format_parse_context& ctx)
  {
    return ctx.begin();
  }

  auto format(RangeFilter filter, std::format_context& ctx) const
  {
    return std::format_to(ctx.out(), "RangeFilter: {} - {}", filter.min_val, filter.max_val);
  }

};

template <>
struct std::formatter<RadFilter, char>
{

  constexpr auto parse(std::format_parse_context& ctx)
  {
    return ctx.begin();
  }

  auto format(RadFilter filter, std::format_context& ctx) const
  {
    return std::format_to(ctx.out(), "RadFilter: origin {} - radius {}", filter.origin, filter.radius);
  }

};

template <>
struct std::formatter<KFilter, char>
{

  constexpr auto parse(std::format_parse_context& ctx)
  {
    return ctx.begin();
  }

  auto format(KFilter filter, std::format_context& ctx) const
  {
    return std::format_to(ctx.out(), "KFilter: origin {} - k {}", filter.origin, filter.k);
  }

};

template <>
struct std::formatter<Filter, char>
{

  constexpr auto parse(std::format_parse_context& ctx)
  {
    return ctx.begin();
  }

  auto format(Filter filter, std::format_context& ctx) const
  {
    auto out = ctx.out();

    out = std::format_to(out, "Filter: iden {} - ", filter.col_identifier);
    out = std::visit([&](auto&& value) {
      return std::format_to(out, "{}", value);
    }, filter.data);

    return out;
  }

};

template <>
struct std::formatter<Expr, char>
{

  constexpr auto parse(std::format_parse_context& ctx)
  {
    return ctx.begin();
  }

  auto format(Expr expr, std::format_context& ctx) const
  {
    return std::visit([&](auto&& value) {
      return std::format_to(ctx.out(), "{}", value);
    }, expr);
  }

};

template <>
struct std::formatter<Column, char>
{

  constexpr auto parse(std::format_parse_context& ctx)
  {
    return ctx.begin();
  }

  auto format(Column col, std::format_context& ctx) const
  {
    return std::format_to(ctx.out(), "Column: {} - {} - INDEX: {}", col.name, col.type, col.index_name ? *col.index_name : "nullopt");
  }

};

template <>
struct std::formatter<InsertValue, char>
{

  constexpr auto parse(std::format_parse_context& ctx)
  {
    return ctx.begin();
  }

  auto format(InsertValue value, std::format_context& ctx) const
  {
    auto out = ctx.out();

    out = std::format_to(out, "( ");

    for (auto& expr : value.exprs) {
      out = std::format_to(out, "{}, ", expr);
    }

    out = std::format_to(out, ")");

    return out;
  }

};

template <>
struct std::formatter<CreateStatement, char>
{

  constexpr auto parse(std::format_parse_context& ctx)
  {
    return ctx.begin();
  }

  auto format(CreateStatement stmt, std::format_context& ctx) const
  {
    auto out = ctx.out();

    out = std::format_to(out, "CREATE TABLE {} - COLUMNS:\n", stmt.table_name);
    for (auto& col : stmt.columns) {
      out = std::format_to(out, "{}\n", col);
    }
    out = std::format_to(out, "FROM FILE: {}", stmt.file_path ? *stmt.file_path : "nullopt");

    return out;
  }

};

template <>
struct std::formatter<SelectStatement, char>
{

  constexpr auto parse(std::format_parse_context& ctx)
  {
    return ctx.begin();
  }

  auto format(SelectStatement stmt, std::format_context& ctx) const
  {
    auto out = ctx.out();

    out = std::format_to(ctx.out(), "SELECT FROM {} -  FILTER: ", stmt.table_name);

    if (stmt.filter)
      out = std::format_to(out, "{}", *stmt.filter);
    else
      out = std::format_to(out, "nullopt");

    return out;
  }

};

template <>
struct std::formatter<InsertStatement, char>
{

  constexpr auto parse(std::format_parse_context& ctx)
  {
    return ctx.begin();
  }

  auto format(InsertStatement stmt, std::format_context& ctx) const
  {
    auto out = ctx.out();

    out = std::format_to(out, "INSERT INTO {} VALUES:\n", stmt.table_name);
    for (auto& insert_val : stmt.values) {
      out = std::format_to(out, "{}\n", insert_val);
    }
    out = std::format_to(out, "EndInsert");

    return out;
  }

};

template <>
struct std::formatter<DeleteStatement, char>
{

  constexpr auto parse(std::format_parse_context& ctx)
  {
    return ctx.begin();
  }

  auto format(DeleteStatement stmt, std::format_context& ctx) const
  {
    return std::format_to(ctx.out(), "DELETE FROM {} -  FILTER: {}", stmt.table_name, stmt.filter);
  }

};

template <>
struct std::formatter<Statement, char>
{

  constexpr auto parse(std::format_parse_context& ctx)
  {
    return ctx.begin();
  }

  auto format(Statement stmt, std::format_context& ctx) const
  {
    return std::visit([&](auto&& value) {
      return std::format_to(ctx.out(), "{}", value);
    }, stmt);
  }

};

template <>
struct std::formatter<SourceFile, char>
{

  constexpr auto parse(std::format_parse_context& ctx)
  {
    return ctx.begin();
  }

  auto format(SourceFile src, std::format_context& ctx) const
  {
    auto out = ctx.out();

    out = std::format_to(out, "Source:\n");
    for (auto& stmt : src.statements) {
      out = std::format_to(out, "{}\n", stmt);
    }
    out = std::format_to(out, "EndSource");

    return out;
  }

};

#endif
