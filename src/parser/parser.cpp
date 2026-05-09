#include "parser/parser.hpp"
#include <cassert>
#include <expected>
#include <print>
#include "parser/ast.hpp"
#include "parser/error.hpp"
#include "parser/token.hpp"
#include "util.hpp"

namespace parser {
    Parser::Parser(std::string source)
        : tokens{std::move(source)} {}

    std::expected<SourceFile, CompileError> Parser::source_file() {
        std::vector<Statement> statements;

        while (!TRY(accept_var<Eof>()))
            statements.emplace_back(TRY(statement()));

        return SourceFile{statements};
    }

    std::expected<Statement, CompileError> Parser::statement() {
        auto res = TRY(tokens.peek()->get());

        if (auto* tok = res.get_if<Keyword>()) {
            switch (*tok) {
                case Keyword::Create:
                    return create_statement();
                case Keyword::Select:
                    return select_statement();
                case Keyword::Insert:
                    return insert_statement();
                case Keyword::Delete:
                    return delete_statement();
                case Keyword::Drop:
                    return drop_statement();
                default:
                    return std::unexpected{ParseError::UnexpectedToken};
            }
        }

        return std::unexpected{ParseError::UnexpectedToken};
    }

    std::expected<CreateStatement, CompileError> Parser::create_statement() {
        TRYV(expect_val<Keyword::Create>());
        TRYV(expect_val<Keyword::Table>());

        bool if_not_exists = false;

        if (TRY(accept_val<Keyword::If>())) {
            TRYV(expect_val<Keyword::Not>());
            TRYV(expect_val<Keyword::Exists>());
            if_not_exists = true;
        }

        auto tbl_name_tok = TRY(expect_var<Identifier>());
        TRYV(expect_val<Symbol::LParen>());

        CreateStatement stmt{tbl_name_tok.value};
        stmt.if_not_exists = if_not_exists;

        do {
            auto tbl_tok = TRY(expect_var<Identifier>());
            auto type_tok = TRY(expect_var<DataType>());

            Column col{tbl_tok.value, type_tok};

            if (TRY(accept_val<Keyword::Index>())) {
                auto iden_tok = TRY(expect_var<Identifier>());
                col.constraint = Index{iden_tok.value};
            } else if (TRY(accept_val<Keyword::Primary>())) {
                TRYV(expect_val<Keyword::Key>());
                col.constraint = PrimaryKey{};
            }

            stmt.columns.emplace_back(std::move(col));
        } while (TRY(accept_val<Symbol::Comma>()));

        TRYV(expect_val<Symbol::RParen>());

        if (TRY(accept_val<Keyword::From>())) {
            TRYV(expect_val<Keyword::File>());

            auto file_tok = TRY(expect_var<Literal>());
            stmt.file_path = file_tok.value;
        }

        TRYV(expect_val<Symbol::SemiColon>());
        return stmt;
    }

    std::expected<SelectStatement, CompileError> Parser::select_statement() {
        TRYV(expect_val<Keyword::Select>());
        TRYV(expect_val<Symbol::Asterisk>());
        TRYV(expect_val<Keyword::From>());
        auto iden_tok = TRY(expect_var<Identifier>());

        SelectStatement stmt{};
        stmt.table_name = iden_tok.value;

        if (TRY(accept_val<Keyword::Where>()))
            stmt.filter = TRY(where_declaration());

        TRYV(expect_val<Symbol::SemiColon>());
        return stmt;
    }

    std::expected<InsertStatement, CompileError> Parser::insert_statement() {
        InsertStatement stmt;

        TRYV(expect_val<Keyword::Insert>());
        TRYV(expect_val<Keyword::Into>());
        auto iden_tok = TRY(expect_var<Identifier>());
        TRYV(expect_val<Keyword::Values>());

        stmt.table_name = iden_tok.value;

        do {
            TRYV(expect_val<Symbol::LParen>());
            auto insert_val = TRY(insert_value());
            TRYV(expect_val<Symbol::RParen>());

            stmt.values.emplace_back(std::move(insert_val));
        } while (TRY(accept_val<Symbol::Comma>()));

        TRYV(expect_val<Symbol::SemiColon>());
        return stmt;
    }

    std::expected<DeleteStatement, CompileError> Parser::delete_statement() {
        TRYV(expect_val<Keyword::Delete>());
        TRYV(expect_val<Keyword::From>());
        auto table_tok = TRY(expect_var<Identifier>());

        DeleteStatement stmt{};
        stmt.table_name = table_tok.value;

        if (TRY(accept_val<Keyword::Where>()))
            stmt.filter = TRY(where_declaration());

        TRYV(expect_val<Symbol::SemiColon>());
        return stmt;
    }

    std::expected<DropStatement, CompileError> Parser::drop_statement() {
        TRYV(expect_val<Keyword::Drop>());
        TRYV(expect_val<Keyword::Table>());

        DropStatement stmt{};

        // FIX: when no if exists is provided, table_name s empty
        if (TRY(accept_val<Keyword::If>())) {
            TRYV(expect_val<Keyword::Exists>());
            stmt.if_exists = true;
        }

        auto table_tok = TRY(expect_var<Identifier>());
        TRYV(expect_val<Symbol::SemiColon>());

        stmt.table_name = table_tok.value;
        return stmt;
    }

    std::expected<InsertValue, CompileError> Parser::insert_value() {
        InsertValue value;

        do {
            if (auto tok = TRY(accept_var<Number>()))
                value.exprs.emplace_back(*tok);
            else if (auto tok = TRY(accept_var<Literal>()))
                value.exprs.emplace_back(*tok);
            else if (TRY(accept_val<Keyword::True>()))
                value.exprs.emplace_back(true);
            else if (TRY(accept_val<Keyword::False>()))
                value.exprs.emplace_back(false);
            else
                return std::unexpected{ParseError::UnexpectedToken};

        } while (TRY(accept_val<Symbol::Comma>()));

        return value;
    }

    std::expected<Filter, CompileError> Parser::where_declaration() {
        Filter filter;

        auto iden_tok = TRY(expect_var<Identifier>());
        filter.col_name = iden_tok.value;

        if (TRY(accept_val<Symbol::Eq>())) {
            auto lit = TRY(expr_lit());
            filter.data = EqFilter{std::move(lit)};
        } else if (TRY(accept_val<Keyword::Between>())) {
            auto low = TRY(expr_lit());
            TRYV(expect_val<Keyword::And>());
            auto high = TRY(expr_lit());

            filter.data = RangeFilter{std::move(low), std::move(high)};
        } else if (TRY(accept_val<Keyword::In>())) {
            TRYV(expect_val<Symbol::LParen>());
            TRYV(expect_val<Keyword::Point>());
            TRYV(expect_val<Symbol::LParen>());
            auto x_tok = TRY(expect_var<Number>());
            TRYV(expect_val<Symbol::Comma>());
            auto y_tok = TRY(expect_var<Number>());
            TRYV(expect_val<Symbol::RParen>());
            TRYV(expect_val<Symbol::Comma>());

            Point2D origin{x_tok.value, y_tok.value};

            if (TRY(accept_val<Keyword::Radius>())) {
                auto radius_tok = TRY(expect_var<Number>());
                filter.data = RadFilter{origin, radius_tok.value};
            } else if (TRY(accept_val<Keyword::K>())) {
                auto k_tok = TRY(expect_var<Number>());
                filter.data = KFilter{origin, static_cast<u64>(k_tok.value)};
            } else {
                return std::unexpected{ParseError::UnexpectedToken};
            }

            TRYV(expect_val<Symbol::RParen>());
        } else {
            return std::unexpected{ParseError::UnexpectedToken};
        }

        return filter;
    }

    std::expected<ExprLit, CompileError> Parser::expr_lit() {
        if (auto tok = TRY(accept_var<Literal>()))
            return tok->value;

        if (auto tok = TRY(accept_var<Number>()))
            return tok->value;

        if (TRY(accept_val<Keyword::True>()))
            return true;

        if (TRY(accept_val<Keyword::False>()))
            return false;

        if (TRY(accept_val<Symbol::LParen>())) {
            auto x_tok = TRY(expect_var<Number>());
            TRYV(expect_val<Symbol::Comma>());
            auto y_tok = TRY(expect_var<Number>());
            TRYV(expect_val<Symbol::RParen>());

            return Point2D(x_tok.value, y_tok.value);
        }

        return std::unexpected{ParseError::UnexpectedToken};
    }

}  // namespace parser
