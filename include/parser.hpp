#ifndef PARSER_HPP
#define PARSER_HPP

#include <expected>
#include <optional>
#include <vector>
#include "ast.hpp"
#include "error.hpp"
#include "peekable.hpp"
#include "util.hpp"

class Parser {
public:
    std::expected<SourceFile, std::vector<CompileError>> source_file();

    explicit Parser(std::string source);

private:
    template<auto>
    std::expected<bool, CompileError> accept_val();

    template<typename T>
    std::expected<std::optional<T>, CompileError> accept_var();

    template<auto>
    std::expected<void, CompileError> expect_val();

    template<typename T>
    std::expected<T, CompileError> expect_var();

    std::expected<Statement, CompileError> statement();

    std::expected<CreateStatement, CompileError> create_statement();
    std::expected<SelectStatement, CompileError> select_statement();
    std::expected<InsertStatement, CompileError> insert_statement();
    std::expected<DeleteStatement, CompileError> delete_statement();

    std::expected<InsertValue, CompileError> insert_value();
    std::expected<Filter, CompileError> where_declaration();

    Peekable tokens;
};

template<auto value>
std::expected<bool, CompileError> Parser::accept_val() {
    using T = decltype(value);

    auto t = TRY(tokens.peek());
    auto* tok = t.get_if<T>();

    if (tok && *tok == value) {
        auto res = tokens.next();
        assert(res.has_value());
        return true;
    }

    return false;
}

template<typename T>
std::expected<std::optional<T>, CompileError> Parser::accept_var() {
    auto t = TRY(tokens.peek());

    if (auto* tok = t.get_if<T>()) {
        auto res = tokens.next();
        assert(res.has_value());
        return *tok;
    }

    return std::nullopt;
}

template<auto value>
std::expected<void, CompileError> Parser::expect_val() {
    using T = decltype(value);

    auto tok = TRY(expect_var<T>());
    if (tok != value)
        return std::unexpected{ParseError::UnexpectedToken};

    return {};
}

template<typename T>
std::expected<T, CompileError> Parser::expect_var() {
    auto t = TRY(tokens.peek());

    if (auto* tok = t.get_if<T>()) {
        auto res = tokens.next();
        assert(res.has_value());
        return *tok;
    }

    return std::unexpected{ParseError::UnexpectedToken};
}

#endif
