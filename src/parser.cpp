#include "parser.hpp"
#include <cassert>
#include "util.hpp"

Parser::Parser(std::string source)
    : tokens{std::move(source)} {}

std::expected<SourceFile, std::vector<CompileError>> Parser::source_file() {
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
            auto result = tokens.next();
            assert(result.has_value());
            auto err = stmt.error();
            errors.emplace_back(err);

            if (std::holds_alternative<LexicalError>(err))
                break;
        }
    }

    if (!errors.empty())
        return std::unexpected{errors};

    return SourceFile{statements};
}

std::expected<Statement, CompileError> Parser::statement() {
    auto res = TRY(tokens.peek());

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
            default:
                return std::unexpected{ParseError::UnexpectedToken};
        }
    }

    return std::unexpected{ParseError::UnexpectedToken};
}

std::expected<CreateStatement, CompileError> Parser::create_statement() {
    CreateStatement stmt;

    TRYV(expect_val<Keyword::Create>());
    TRYV(expect_val<Keyword::Table>());

    auto tok = TRY(expect_var<Identifier>());
    stmt.table_name = tok.value;

    TRYV(expect_val<Symbol::LParen>());

    while (true) {
        Column col{};

        auto tok = TRY(expect_var<Identifier>());
        col.name = tok.value;
        col.type = TRY(expect_var<DataType>());

        if (TRY(accept_val<Keyword::Index>())) {
            auto tok = TRY(expect_var<Identifier>());
            col.index_name = tok.value;
        }

        stmt.columns.emplace_back(std::move(col));

        if (!TRY(accept_val<Symbol::Comma>()))
            break;
    }

    TRYV(expect_val<Symbol::RParen>());

    if (TRY(accept_val<Keyword::From>())) {
        TRYV(expect_val<Keyword::File>());

        auto tok = TRY(expect_var<Literal>());
        stmt.file_path = tok.value;
    }

    TRYV(expect_val<Symbol::SemiColon>());
    return stmt;
}

std::expected<SelectStatement, CompileError> Parser::select_statement() {
    SelectStatement stmt;

    TRYV(expect_val<Keyword::Select>());
    TRYV(expect_val<Symbol::Asterisk>());
    TRYV(expect_val<Keyword::From>());

    auto iden_tok = TRY(expect_var<Identifier>());
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
    auto tok = TRY(expect_var<Identifier>());

    DeleteStatement stmt{};
    stmt.table_name = tok.value;

    if (TRY(accept_val<Keyword::Where>()))
        stmt.filter = TRY(where_declaration());

    TRYV(expect_val<Symbol::SemiColon>());
    return stmt;
}

std::expected<InsertValue, CompileError> Parser::insert_value() {
    InsertValue value;

    do {
        if (auto tok = TRY(accept_var<Literal>())) {
            value.exprs.emplace_back(std::move(*tok));
        } else if (auto tok = TRY(accept_var<Literal>())) {
            value.exprs.emplace_back(std::move(*tok));
        } else {
            return std::unexpected{ParseError::UnexpectedToken};
        }
    } while (TRY(accept_val<Symbol::Comma>()));

    return value;
}

std::expected<Filter, CompileError> Parser::where_declaration() {
    Filter filter;

    auto iden_tok = TRY(expect_var<Identifier>());
    filter.col_identifier = iden_tok.value;

    if (TRY(accept_val<Symbol::Eq>())) {
        EqFilter eqfilter;

        auto res = TRY(tokens.next());

        if (auto* tok = res.get_if<Literal>())
            eqfilter.value = std::move(tok->value);
        else if (auto* tok = res.get_if<Number>())
            eqfilter.value = tok->value;
        else
            return std::unexpected{ParseError::UnexpectedToken};

        filter.data = std::move(eqfilter);
    } else if (TRY(accept_val<Keyword::Between>())) {
        auto low_tok = TRY(expect_var<Number>());
        TRYV(expect_val<Keyword::And>());
        auto high_tok = TRY(expect_var<Number>());

        filter.data = RangeFilter{low_tok.value, high_tok.value};
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
            filter.data = KFilter{origin, k_tok.value};
        } else {
            return std::unexpected{ParseError::UnexpectedToken};
        }

        TRYV(expect_val<Symbol::RParen>());
    } else {
        return std::unexpected{ParseError::UnexpectedToken};
    }

    return filter;
}
