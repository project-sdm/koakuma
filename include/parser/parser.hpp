#ifndef PARSER_HPP
#define PARSER_HPP

#include <expected>
#include <optional>
#include "ast.hpp"
#include "error.hpp"
#include "parser/lexer.hpp"
#include "util.hpp"

namespace parser {
    class Parser {
    public:
        std::expected<SourceFile, CompileError> source_file();

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
        std::expected<DropStatement, CompileError> drop_statement();

        std::expected<InsertValue, CompileError> insert_value();
        std::expected<ExprLit, CompileError> expr_lit();
        std::expected<Filter, CompileError> where_declaration();

        util::Peekable<Lexer> tokens;
    };

    template<auto value>
    std::expected<bool, CompileError> Parser::accept_val() {
        using T = decltype(value);

        auto t = TRY_COPY(tokens.peek()->get());
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
        auto t = TRY_COPY(tokens.peek()->get());

        if (t.get_if<T>()) {
            auto res = tokens.next();
            return (*res)->get<T>();
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
        auto t_opt = tokens.next();
        auto t = TRY(*t_opt);

        if (auto* tok = t.get_if<T>())
            return T{*tok};

        return std::unexpected{ParseError::UnexpectedToken};
    }

}  // namespace parser

#endif
