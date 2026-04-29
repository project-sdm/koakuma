#include "query_executor.hpp"
#include <concepts>
#include <memory>
#include <optional>
#include <print>
#include <utility>
#include <variant>
#include <vector>
#include "catalog.hpp"
#include "engine/engine.hpp"
#include "parser/ast.hpp"
#include "parser/token.hpp"
#include "seq_file.hpp"

namespace volcano {

    template<typename T>
    concept volcano_iterator = requires(T iter) {
        { iter.next() } -> std::same_as<std::optional<Row>>;
    };

    class VolcanoIterator {
        class Contract;

    public:
        std::optional<Row> next() {
            return self->next();
        }

        template<volcano_iterator It>
        explicit VolcanoIterator(It iter)
            : self{std::make_unique<Wrapper<It>>(std::move(iter))} {}

    private:
        class Contract {
        public:
            Contract() = default;
            Contract(const Contract&) = default;
            Contract(Contract&&) = delete;
            Contract& operator=(const Contract&) = default;
            Contract& operator=(Contract&&) = delete;

            virtual std::optional<Row> next() = 0;

            virtual ~Contract() = default;
        };

        template<volcano_iterator It>
        class Wrapper final : public Contract {
        public:
            std::optional<Row> next() override {
                return iterator.next();
            }

            explicit Wrapper(It iterator)
                : iterator{std::move(iterator)} {}

        private:
            It iterator;
        };

        std::unique_ptr<Contract> self;
    };

    class SeqScan {
    public:
        std::optional<Row> next() {
            return cursor.next();
        }

        explicit SeqScan(SeqFile::Cursor cursor)
            : cursor{std::move(cursor)} {}

    private:
        SeqFile::Cursor cursor;
    };

    struct FilterVisitor {
        bool operator()(int /*value*/) {
            return false;
        }
        bool operator()(bool /*value*/) {
            return false;
        }
        bool operator()(const std::string& value) {
            if (auto* filter = std::get_if<parser::EqFilter>(&data))
                if (auto* value_to_compare = std::get_if<std::string>(&filter->value))
                    return value == *value_to_compare;

            return false;
        }

        parser::FilterData data;
    };

    class Filter {
    public:
        std::optional<Row> next() {
            while (auto row = child.next()) {
                for (std::size_t i = 0; i < meta.columns.size(); ++i) {
                    const auto& col = meta.columns.at(i);
                    if (col.name == filter.col_identifier &&
                        std::visit(FilterVisitor{filter.data}, row->at(i)))
                        return row;
                }
            }

            return std::nullopt;
        }

        Filter(VolcanoIterator child, parser::Filter filter, SeqFile::Meta meta)
            : child{std::move(child)},
              filter{std::move(filter)},
              meta{std::move(meta)} {}

        VolcanoIterator child;
        parser::Filter filter;
        SeqFile::Meta meta;
    };

}  // namespace volcano

namespace {

    ColumnType get_col_type(DataType col) {
        switch (col) {
            case DataType::Bool:
                return ColumnType::BOOL;
            case DataType::Date:
                return ColumnType::STRING;
            case DataType::Int:
                return ColumnType::INT;
            case DataType::Real:
                return ColumnType::INT;  // TODO: add float type
            case DataType::Text:
                return ColumnType::STRING;
            case DataType::Uuid:
                return ColumnType::STRING;
            case DataType::Varchar:
                return ColumnType::STRING;
        }
        std::unreachable();
    }

    struct ExprMapper {
        Value operator()(bool expr) {
            return expr;
        }
        Value operator()(Number expr) {
            return static_cast<int>(expr.value);
        }
        Value operator()(const Literal& expr) {
            return expr.value;
        }
    };

    Row map_exprs(const std::vector<parser::Expr>& exprs) {
        Row row;
        row.reserve(exprs.size());

        for (const auto& expr : exprs) {
            row.emplace_back(std::visit(ExprMapper{}, expr));
        }
        return row;
    }

}  // namespace

QueryExecutor::QueryExecutor(Engine& engine)
    : engine{engine} {}

void QueryExecutor::exec(const parser::SourceFile& source_file) {
    Executor executor{engine};

    for (const auto& stmt : source_file.statements)
        std::visit(executor, stmt);
}

void QueryExecutor::Executor::operator()(const parser::CreateStatement& stmt) const {
    std::size_t n_columns = stmt.columns.size();

    std::vector<Column> columns;
    columns.reserve(n_columns);

    std::size_t pkey_col = 0;
    for (std::size_t i = 0; i < n_columns; ++i) {
        const auto& col = stmt.columns[i];
        if (col.constraint && std::holds_alternative<parser::PrimaryKey>(*col.constraint))
            pkey_col = i;

        columns.emplace_back(col.name, get_col_type(col.type));
    }

    bool created = catalog::create_table(engine, stmt.table_name, columns, pkey_col);

    if (!created) {
        std::println("table {} already exists", stmt.table_name);
        return;
    }

    if (stmt.file_path) {
        // std::string_view path = *stmt.file_path;
        // TODO: load csv file
    }
}

void QueryExecutor::Executor::operator()(const parser::SelectStatement& stmt) const {
    auto table = catalog::get_table(engine, stmt.table_name);
    if (!table) {
        std::println("table {} does not exist", stmt.table_name);
        return;
    }

    auto root = volcano::VolcanoIterator{volcano::SeqScan{table->cursor()}};

    if (stmt.filter) {
        auto meta = table->get_meta();
        root = volcano::VolcanoIterator{
            volcano::Filter{std::move(root), *stmt.filter, std::move(meta)}
        };
    }

    while (auto row = root.next())
        std::println("{}", *row);  // in general, do stuff with result
}

void QueryExecutor::Executor::operator()(const parser::InsertStatement& stmt) const {
    auto table = catalog::get_table(engine, stmt.table_name);
    if (!table) {
        std::println("table {} does not exist", stmt.table_name);
        return;
    }

    for (const auto& value : stmt.values) {
        Row row = map_exprs(value.exprs);
        table->insert(row);
    }
}

// TODO
// bool applies(const Row& row, parser::Filter filter);

void QueryExecutor::Executor::operator()(const parser::DeleteStatement& stmt) const {
    auto table = catalog::get_table(engine, stmt.table_name);
    if (!table) {
        std::println("table {} does not exist", stmt.table_name);
        return;
    }

    auto cursor = table->cursor();

    while (auto row = cursor.next()) {
        if (!stmt.filter.has_value() /* || applies(row, *stmt.filter) */)
            return;  // TODO: delete
    }
}
