#ifndef QUERY_EXECUTOR_HPP
#define QUERY_EXECUTOR_HPP

#include <expected>
#include <string>
#include <variant>
#include "catalog.hpp"
#include "engine/engine.hpp"
#include "magic_enum/magic_enum.hpp"
#include "parser/ast.hpp"
#include "seq_file.hpp"

namespace volcano {

    class VolcanoIterator;

}  // namespace volcano

struct UnexpectedType {
    parser::ExprLit lit;
    ColumnType expected_type;

    UnexpectedType(parser::ExprLit lit, ColumnType expected_type);
};

struct TableNotFound {
    std::string table_name;

    explicit TableNotFound(std::string table_name);
};

struct TableAlreadyExists {
    std::string table_name;

    explicit TableAlreadyExists(std::string table_name);
};

struct InvalidCsvCell {
    std::string text;
    ColumnType expected_type;

    InvalidCsvCell(std::string text, ColumnType expected_type);
};

using ExecutionError = std::variant<catalog::InsertionError,
                                    UnexpectedType,
                                    TableNotFound,
                                    TableAlreadyExists,
                                    InvalidCsvCell>;

class QueryExecutor {
public:
    struct RowSink {
        virtual void on_columns(const std::vector<Column>& columns) = 0;
        virtual void on_row(const Row& row) = 0;
        virtual ~RowSink() = default;
    };

    explicit QueryExecutor(Engine& engine);

    std::expected<u32, ExecutionError> exec(const catalog::Catalog& catalog,
                                            const parser::SourceFile& source_file,
                                            RowSink& sink);

private:
    struct Executor {
        Engine& eng;
        const catalog::Catalog& catalog;
        RowSink& sink;

        std::expected<void, ExecutionError> operator()(const parser::CreateStatement& stmt) const;
        std::expected<void, ExecutionError> operator()(const parser::SelectStatement& stmt);
        std::expected<void, ExecutionError> operator()(const parser::InsertStatement& stmt) const;
        std::expected<void, ExecutionError> operator()(const parser::DeleteStatement& stmt) const;

        [[nodiscard]] static std::expected<volcano::VolcanoIterator, ExecutionError> apply_filter(
            volcano::VolcanoIterator iter,
            catalog::Table& table,
            const parser::Filter& filter);
    };

    Engine& engine;
};

template<>
struct std::formatter<UnexpectedType, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const UnexpectedType& err, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Could not cast {} as {}", err.lit,
                              magic_enum::enum_name(err.expected_type));
    }
};

template<>
struct std::formatter<TableNotFound, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const TableNotFound& err, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Table '{}' does not exist", err.table_name);
    }
};

template<>
struct std::formatter<TableAlreadyExists, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const TableAlreadyExists& err, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Table {} already exists", err.table_name);
    }
};

template<>
struct std::formatter<InvalidCsvCell, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const InvalidCsvCell& err, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Could not parse CSV cell `{}` as {}", err.text,
                              magic_enum::enum_name(err.expected_type));
    }
};

template<>
struct std::formatter<ExecutionError, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const ExecutionError& err, std::format_context& ctx) {
        return std::visit([&](auto&& v) { return std::format_to(ctx.out(), "{}", v); }, err);
    }
};

#endif
