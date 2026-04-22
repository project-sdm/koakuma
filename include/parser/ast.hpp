#ifndef AST_HPP
#define AST_HPP

#include <format>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "token.hpp"

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

    struct EqFilter {
        std::variant<std::string, f64> value;
    };

    struct RangeFilter {
        f64 min_val;
        f64 max_val;

        RangeFilter(f64 min_val, f64 max_val);
    };

    struct Point2D {
        f64 x;
        f64 y;

        Point2D(f64 x, f64 y);
    };

    struct RadFilter {
        Point2D origin;
        f64 radius;

        RadFilter(Point2D origin, f64 radius);
    };

    struct KFilter {
        Point2D origin;
        f64 k;

        KFilter(Point2D origin, f64 k);
    };

    using FilterData = std::variant<EqFilter, RangeFilter, RadFilter, KFilter>;

    struct Filter {
        std::string col_identifier;
        FilterData data;
    };

    using Expr = std::variant<Literal, Number>;

    struct InsertValue {
        std::vector<Expr> exprs;
    };

    struct CreateStatement {
        std::string table_name;
        std::vector<Column> columns;
        std::optional<std::string> file_path = std::nullopt;

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
        std::optional<Filter> filter;
    };

    using Statement =
        std::variant<CreateStatement, SelectStatement, InsertStatement, DeleteStatement>;

    struct SourceFile {
        std::vector<Statement> statements;
    };

}  // namespace parser

template<>
struct std::formatter<parser::Point2D, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(parser::Point2D point, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Point2D({}, {})", point.x, point.y);
    }
};

template<>
struct std::formatter<parser::EqFilter, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(parser::EqFilter filter, std::format_context& ctx) {
        return std::visit(
            [&](auto&& value) { return std::format_to(ctx.out(), "EqFilter: {}", value); },
            filter.value);
    }
};

template<>
struct std::formatter<parser::RangeFilter, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(parser::RangeFilter filter, std::format_context& ctx) {
        return std::format_to(ctx.out(), "RangeFilter: {} - {}", filter.min_val, filter.max_val);
    }
};

template<>
struct std::formatter<parser::RadFilter, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(parser::RadFilter filter, std::format_context& ctx) {
        return std::format_to(ctx.out(), "RadFilter: origin {} - radius {}", filter.origin,
                              filter.radius);
    }
};

template<>
struct std::formatter<parser::KFilter, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(parser::KFilter filter, std::format_context& ctx) {
        return std::format_to(ctx.out(), "KFilter: origin {} - k {}", filter.origin, filter.k);
    }
};

template<>
struct std::formatter<parser::Filter, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(parser::Filter filter, std::format_context& ctx) {
        auto out = ctx.out();

        out = std::format_to(out, "Filter: iden {} - ", filter.col_identifier);
        out =
            std::visit([&](auto&& value) { return std::format_to(out, "{}", value); }, filter.data);

        return out;
    }
};

template<>
struct std::formatter<parser::Expr, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(parser::Expr expr, std::format_context& ctx) {
        return std::visit([&](auto&& value) { return std::format_to(ctx.out(), "{}", value); },
                          expr);
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

    static auto format(parser::Index index, std::format_context& ctx) {
        return std::format_to(ctx.out(), "INDEX {}", index.name);
    }
};

template<>
struct std::formatter<parser::Constraint, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(parser::Constraint constraint, std::format_context& ctx) {
        return std::visit([&](auto&& value) { return std::format_to(ctx.out(), "{}", value); },
                          constraint);
    }
};

template<typename T>
struct std::formatter<std::optional<T>, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(std::optional<T> opt, std::format_context& ctx) {
        if (!opt)
            return std::format_to(ctx.out(), "nullopt");

        return std::format_to(ctx.out(), "{}", *opt);
    }
};

template<>
struct std::formatter<parser::Column, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(parser::Column col, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Column: {} - {} - CONSTRAINT: {}", col.name, col.type,
                              col.constraint);
    }
};

template<>
struct std::formatter<parser::InsertValue, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(parser::InsertValue value, std::format_context& ctx) {
        auto out = ctx.out();

        out = std::format_to(out, "( ");

        for (auto& expr : value.exprs) {
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

    static auto format(parser::CreateStatement stmt, std::format_context& ctx) {
        auto out = ctx.out();

        out = std::format_to(out, "CREATE TABLE {} - COLUMNS:\n", stmt.table_name);
        for (auto& col : stmt.columns) {
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

    static auto format(parser::SelectStatement stmt, std::format_context& ctx) {
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

    static auto format(parser::InsertStatement stmt, std::format_context& ctx) {
        auto out = ctx.out();

        out = std::format_to(out, "INSERT INTO {} VALUES:\n", stmt.table_name);
        for (auto& insert_val : stmt.values) {
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

    static auto format(parser::DeleteStatement stmt, std::format_context& ctx) {
        if (stmt.filter) {
            return std::format_to(ctx.out(), "DELETE FROM {} -  FILTER: {}", stmt.table_name,
                                  *stmt.filter);
        }

        return std::format_to(ctx.out(), "DELETE FROM {} ", stmt.table_name);
    }
};

template<>
struct std::formatter<parser::Statement, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(parser::Statement stmt, std::format_context& ctx) {
        return std::visit([&](auto&& value) { return std::format_to(ctx.out(), "{}", value); },
                          stmt);
    }
};

template<>
struct std::formatter<parser::SourceFile, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(parser::SourceFile src, std::format_context& ctx) {
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
