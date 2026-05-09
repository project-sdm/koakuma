#ifndef AST_HPP
#define AST_HPP

#include <format>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "token.hpp"
#include "types.hpp"

namespace parser {

    struct PrimaryKey {};

    struct Index {
        std::string name;
    };

    using Constraint = std::variant<PrimaryKey, Index>;

    struct Column {
        std::string name;
        DataType type;
        std::optional<Constraint> constraint;

        Column(std::string name, DataType type);
    };

    struct Point2D {
        f64 x = 0;
        f64 y = 0;

        Point2D();
        Point2D(f64 x, f64 y);

        bool operator==(const Point2D& other) const;
        bool operator<(const Point2D& other) const;
        bool operator<=(const Point2D& other) const;
        bool operator>(const Point2D& other) const;
        bool operator>=(const Point2D& other) const;
    };

    using ExprLit = std::variant<std::string, f64, bool, Point2D>;

    struct EqFilter {
        ExprLit value;
    };

    struct RangeFilter {
        ExprLit low;
        ExprLit high;

        RangeFilter(ExprLit low, ExprLit high);
    };

    struct RadFilter {
        Point2D origin;
        f64 radius;

        RadFilter(Point2D origin, f64 radius);
    };

    struct KFilter {
        Point2D origin;
        u64 k;

        KFilter(Point2D origin, u64 k);
    };

    using FilterData = std::variant<EqFilter, RangeFilter, RadFilter, KFilter>;

    struct Filter {
        std::string col_name;
        FilterData data;
    };

    struct InsertValue {
        std::vector<ExprLit> exprs;
    };

    struct CreateStatement {
        std::string table_name;
        std::vector<Column> columns;
        std::optional<std::string> file_path = std::nullopt;
        bool if_not_exists = false;

        explicit CreateStatement(std::string table_name);
    };

    struct SelectStatement {
        std::string table_name;
        std::optional<Filter> filter = std::nullopt;
    };

    struct InsertStatement {
        std::string table_name;
        std::vector<InsertValue> values;
    };

    struct DeleteStatement {
        std::string table_name;
        std::string col_name;
        ExprLit value;
    };

    struct DropStatement {
        std::string table_name;
        bool if_exists = false;
    };

    struct ShowStatement {
        std::string table_name;
        std::string col_name;
    };

    using Statement = std::variant<CreateStatement,
                                   SelectStatement,
                                   InsertStatement,
                                   DeleteStatement,
                                   DropStatement,
                                   ShowStatement>;

    struct SourceFile {
        std::vector<Statement> statements;
    };

}  // namespace parser

template<>
struct std::formatter<parser::Point2D, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::Point2D& point, std::format_context& ctx) {
        return std::format_to(ctx.out(), "({}, {})", point.x, point.y);
    }
};

template<>
struct std::formatter<parser::EqFilter, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::EqFilter& filter, std::format_context& ctx) {
        return std::visit(
            [&](auto&& value) { return std::format_to(ctx.out(), "EqFilter: {}", value); },
            filter.value);
    }
};

template<>
struct std::formatter<parser::ExprLit, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::ExprLit& lit, std::format_context& ctx) {
        if (const auto* s = std::get_if<std::string>(&lit))
            return std::format_to(ctx.out(), "'{}'", *s);

        return std::visit([&](auto&& v) { return std::format_to(ctx.out(), "{}", v); }, lit);
    }
};

template<>
struct std::formatter<parser::RangeFilter, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::RangeFilter& filter, std::format_context& ctx) {
        return std::format_to(ctx.out(), "RangeFilter: {} - {}", filter.low, filter.high);
    }
};

template<>
struct std::formatter<parser::RadFilter, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::RadFilter& filter, std::format_context& ctx) {
        return std::format_to(ctx.out(), "RadFilter: origin {} - radius {}", filter.origin,
                              filter.radius);
    }
};

template<>
struct std::formatter<parser::KFilter, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::KFilter& filter, std::format_context& ctx) {
        return std::format_to(ctx.out(), "KFilter: origin {} - k {}", filter.origin, filter.k);
    }
};

template<>
struct std::formatter<parser::Filter, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::Filter& filter, std::format_context& ctx) {
        auto out = ctx.out();

        out = std::format_to(out, "Filter: iden {} - ", filter.col_name);
        out =
            std::visit([&](auto&& value) { return std::format_to(out, "{}", value); }, filter.data);

        return out;
    }
};

template<>
struct std::formatter<parser::PrimaryKey, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format([[maybe_unused]] parser::PrimaryKey pkey, std::format_context& ctx) {
        return std::format_to(ctx.out(), "PRIMARY KEY");
    }
};

template<>
struct std::formatter<parser::Index, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::Index& index, std::format_context& ctx) {
        return std::format_to(ctx.out(), "INDEX {}", index.name);
    }
};

template<>
struct std::formatter<parser::Constraint, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::Constraint& constraint, std::format_context& ctx) {
        return std::visit([&](auto&& value) { return std::format_to(ctx.out(), "{}", value); },
                          constraint);
    }
};

template<>
struct std::formatter<parser::Column, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::Column& col, std::format_context& ctx) {
        if (!col.constraint)
            return std::format_to(ctx.out(), "Column: {} - {}", col.name, col.type);

        return std::format_to(ctx.out(), "Column: {} - {} - CONSTRAINT: {}", col.name, col.type,
                              *col.constraint);
    }
};

template<>
struct std::formatter<parser::InsertValue, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::InsertValue& value, std::format_context& ctx) {
        auto out = ctx.out();

        out = std::format_to(out, "( ");

        for (const auto& expr : value.exprs) {
            out = std::format_to(out, "{}, ", expr);
        }

        out = std::format_to(out, ")");

        return out;
    }
};

template<>
struct std::formatter<parser::CreateStatement, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::CreateStatement& stmt, std::format_context& ctx) {
        auto out = ctx.out();

        out = std::format_to(out, "CREATE TABLE {} - COLUMNS:\n", stmt.table_name);
        for (const auto& col : stmt.columns) {
            out = std::format_to(out, "{}\n", col);
        }
        out = std::format_to(out, "FROM FILE: {}", stmt.file_path ? *stmt.file_path : "nullopt");

        return out;
    }
};

template<>
struct std::formatter<parser::SelectStatement, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::SelectStatement& stmt, std::format_context& ctx) {
        auto out = ctx.out();

        out = std::format_to(ctx.out(), "SELECT FROM {} -  FILTER: ", stmt.table_name);

        if (stmt.filter)
            out = std::format_to(out, "{}", *stmt.filter);
        else
            out = std::format_to(out, "nullopt");

        return out;
    }
};

template<>
struct std::formatter<parser::InsertStatement, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::InsertStatement& stmt, std::format_context& ctx) {
        auto out = ctx.out();

        out = std::format_to(out, "INSERT INTO {} VALUES:\n", stmt.table_name);
        for (const auto& insert_val : stmt.values) {
            out = std::format_to(out, "{}\n", insert_val);
        }
        out = std::format_to(out, "EndInsert");

        return out;
    }
};

template<>
struct std::formatter<parser::DeleteStatement, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::DeleteStatement& stmt, std::format_context& ctx) {
        return std::format_to(ctx.out(), "DELETE FROM {} WHERE {} = {}", stmt.table_name,
                              stmt.col_name, stmt.value);
    }
};

template<>
struct std::formatter<parser::DropStatement, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::DropStatement& stmt, std::format_context& ctx) {
        return std::format_to(ctx.out(), "DROP TABLE {} - if exists: {} ", stmt.table_name,
                              stmt.if_exists);
    }
};

template<>
struct std::formatter<parser::ShowStatement, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::ShowStatement& stmt, std::format_context& ctx) {
        return std::format_to(ctx.out(), "SHOW INDEX IN TABLE {} FOR COLUMN {} ", stmt.table_name,
                              stmt.col_name);
    }
};

template<>
struct std::formatter<parser::Statement, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::Statement& stmt, std::format_context& ctx) {
        return std::visit([&](auto&& value) { return std::format_to(ctx.out(), "{}", value); },
                          stmt);
    }
};

template<>
struct std::formatter<parser::SourceFile, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const parser::SourceFile& src, std::format_context& ctx) {
        auto out = ctx.out();

        out = std::format_to(out, "Source:\n");
        for (const auto& stmt : src.statements) {
            out = std::format_to(out, "{}\n", stmt);
        }
        out = std::format_to(out, "EndSource");

        return out;
    }
};

#endif
