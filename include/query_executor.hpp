#ifndef QUERY_EXECUTOR_HPP
#define QUERY_EXECUTOR_HPP

#include <cstddef>
#include <expected>
#include <string>
#include <variant>
#include "catalog.hpp"
#include "engine/engine.hpp"
#include "index/rtree.hpp"
#include "magic_enum/magic_enum.hpp"
#include "parser/ast.hpp"
#include "types.hpp"

namespace volcano {

    class VolcanoIterator;

}  // namespace volcano

struct UnexpectedType {
    parser::ExprLit lit;
    ColumnType expected_type;

    UnexpectedType(parser::ExprLit lit, ColumnType expected_type);
};

struct ColumnCountMismatch {
    std::size_t expected;
    std::size_t got;

    ColumnCountMismatch(std::size_t expected, std::size_t got);
};

struct TableNotFound {
    std::string table_name;

    explicit TableNotFound(std::string table_name);
};

struct ColumnNotFound {
    std::string table_name;
    std::string col_name;

    explicit ColumnNotFound(std::string table_name, std::string col_name);
};

struct TableAlreadyExists {
    std::string table_name;

    explicit TableAlreadyExists(std::string table_name);
};

struct FileNotFound {
    std::filesystem::path path;

    explicit FileNotFound(std::filesystem::path path);
};

struct InvalidIndexName {
    std::string name;

    explicit InvalidIndexName(std::string name);
};

struct InvalidCsvSchema {};

struct InvalidCsvCell {
    std::string text;
    ColumnType expected_type;

    InvalidCsvCell(std::string text, ColumnType expected_type);
};

struct UnsupportedOperation {
    std::string col_name;

    explicit UnsupportedOperation(std::string col_name);
};

using ExecutionError = std::variant<catalog::InsertionError,
                                    catalog::CreateTableError,
                                    ColumnCountMismatch,
                                    UnexpectedType,
                                    TableNotFound,
                                    ColumnNotFound,
                                    TableAlreadyExists,
                                    InvalidCsvSchema,
                                    InvalidCsvCell,
                                    FileNotFound,
                                    InvalidIndexName,
                                    UnsupportedOperation>;

class QueryExecutor {
public:
    struct RowSink {
        virtual void on_begin() = 0;

        virtual void on_table(const std::vector<Column>& columns) = 0;
        virtual void on_row(const Row& row) = 0;

        virtual void on_plane() = 0;
        virtual void on_rect(u64 level, const Rect<2>& rect) = 0;

        virtual void on_message(const std::string& message) = 0;
        virtual void on_warning(const std::string& warning) = 0;
        virtual void on_error(const std::string& error) = 0;

        virtual ~RowSink() = default;
    };

    explicit QueryExecutor(Engine& engine);

    std::expected<void, ExecutionError> exec(const catalog::Catalog& catalog,
                                             const parser::SourceFile& source_file,
                                             RowSink& sink);

private:
    struct Executor {
        Engine& eng;
        const catalog::Catalog& catalog;
        RowSink& sink;

        Executor(Engine& eng, const catalog::Catalog& catalog, RowSink& sink);

        std::expected<void, ExecutionError> operator()(const parser::CreateStatement& stmt) const;
        std::expected<void, ExecutionError> operator()(const parser::SelectStatement& stmt);
        std::expected<void, ExecutionError> operator()(const parser::InsertStatement& stmt) const;
        std::expected<void, ExecutionError> operator()(const parser::DeleteStatement& stmt) const;
        std::expected<void, ExecutionError> operator()(const parser::DropStatement& stmt) const;
        std::expected<void, ExecutionError> operator()(const parser::ShowStatement& stmt) const;

        [[nodiscard]] static std::expected<volcano::VolcanoIterator, ExecutionError> apply_filter(
            volcano::VolcanoIterator iter,
            catalog::Table& table,
            const parser::Filter& filter,
            RowSink& sink);
    };

    Engine& engine;
};

template<>
struct std::formatter<UnexpectedType, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const UnexpectedType& err, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Could not cast {} as {}.", err.lit,
                              magic_enum::enum_name(err.expected_type));
    }
};

template<>
struct std::formatter<TableNotFound, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const TableNotFound& err, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Table '{}' does not exist.", err.table_name);
    }
};

template<>
struct std::formatter<ColumnNotFound, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const ColumnNotFound& err, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Table '{}' does not have column {}.", err.table_name,
                              err.col_name);
    }
};

template<>
struct std::formatter<TableAlreadyExists, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const TableAlreadyExists& err, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Table '{}' already exists.", err.table_name);
    }
};

template<>
struct std::formatter<InvalidCsvSchema, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format([[maybe_unused]] const InvalidCsvSchema& err, std::format_context& ctx) {
        return std::format_to(ctx.out(), "CSV does not match table schema.");
    }
};

template<>
struct std::formatter<InvalidCsvCell, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const InvalidCsvCell& err, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Could not parse CSV cell `{}` as {}.", err.text,
                              magic_enum::enum_name(err.expected_type));
    }
};

template<>
struct std::formatter<FileNotFound, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const FileNotFound& err, std::format_context& ctx) {
        return std::format_to(ctx.out(), "File '{}' not found.", err.path.string());
    }
};

template<>
struct std::formatter<InvalidIndexName, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const InvalidIndexName& err, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Invalid index: '{}'", err.name);
    }
};

template<>
struct std::formatter<UnsupportedOperation, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const UnsupportedOperation& err, std::format_context& ctx) {
        return std::format_to(
            ctx.out(), "Unsupported operation on column '{}' without proper index.", err.col_name);
    }
};

template<>
struct std::formatter<ColumnCountMismatch, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const ColumnCountMismatch& err, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Column count mismatch (expected {} values, got {})",
                              err.expected, err.got);
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
