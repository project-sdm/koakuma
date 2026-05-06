#include "query_executor.hpp"
#include <cassert>
#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <print>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "catalog.hpp"
#include "engine/engine.hpp"
#include "parser/ast.hpp"
#include "parser/token.hpp"
#include "rapidcsv/rapidcsv.h"
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
            return TRY_OPT(cursor.next()).second;
        }

        explicit SeqScan(SeqFile::Cursor cursor)
            : cursor{std::move(cursor)} {}

    private:
        SeqFile::Cursor cursor;
    };

    class FilterVisitor {
    public:
        explicit FilterVisitor(parser::FilterData data)
            : data{std::move(data)} {}

        bool operator()(int value) {
            if (auto* filter = std::get_if<parser::EqFilter>(&data))
                if (auto* value_to_compare = std::get_if<f64>(&filter->value))
                    return value == *value_to_compare;

            if (auto* filter = std::get_if<parser::RangeFilter>(&data))
                return filter->min_val <= value && value <= filter->max_val;

            return false;
        }
        bool operator()(f64 value) {
            if (auto* filter = std::get_if<parser::EqFilter>(&data))
                if (auto* value_to_compare = std::get_if<f64>(&filter->value))
                    return value == *value_to_compare;

            if (auto* filter = std::get_if<parser::RangeFilter>(&data))
                return filter->min_val <= value && value <= filter->max_val;

            return false;
        }
        bool operator()(bool value) {
            if (auto* filter = std::get_if<parser::EqFilter>(&data))
                if (auto* value_to_compare = std::get_if<bool>(&filter->value))
                    return value == *value_to_compare;

            return false;
        }
        bool operator()(const std::string& value) {
            if (auto* filter = std::get_if<parser::EqFilter>(&data))
                if (auto* value_to_compare = std::get_if<std::string>(&filter->value))
                    return value == *value_to_compare;

            return false;
        }

    private:
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

    private:
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
            case DataType::Text:
            case DataType::Uuid:
                return ColumnType::STRING;
            case DataType::Int:
                return ColumnType::INT;
            case DataType::Real:
                return ColumnType::FLOAT;
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
            return expr.value;
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

u32 QueryExecutor::exec(const catalog::Catalog& catalog,
                        const parser::SourceFile& source_file,
                        RowSink& sink) {
    Executor executor{engine, catalog, sink};

    for (const auto& stmt : source_file.statements) {
        std::println("==================================");
        std::visit(executor, stmt);
        std::println();
    }

    return static_cast<u32>(source_file.statements.size());
}

void QueryExecutor::Executor::operator()(const parser::CreateStatement& stmt) const {
    std::size_t n_columns = stmt.columns.size();

    std::vector<Column> columns;
    columns.reserve(n_columns);

    std::size_t pkey_col = 0;
    for (std::size_t i = 0; i < n_columns; ++i) {
        const auto& col = stmt.columns[i];
        std::optional<IndexType> col_index = std::nullopt;

        if (col.constraint) {
            if (std::holds_alternative<parser::PrimaryKey>(*col.constraint)) {
                pkey_col = i;
            } else if (const auto* index = std::get_if<parser::Index>(&*col.constraint)) {
                if (index->name == "hash")
                    col_index = IndexType::HASH;
                else if (index->name == "btree")
                    col_index = IndexType::BTREE;
                else
                    std::println("invalid index name: `{}`", index->name);
            }
        }

        columns.emplace_back(col.name, get_col_type(col.type), col_index);
    }

    bool created = catalog.create_table(engine, stmt.table_name, columns, pkey_col);

    if (!created) {
        std::println("table {} already exists", stmt.table_name);
        return;
    }

    if (stmt.file_path) {
        // load from csv
        auto table = catalog.get_table(engine, stmt.table_name);
        assert(table);

        rapidcsv::Document doc{*stmt.file_path};

        if (doc.GetColumnCount() != columns.size())
            throw std::runtime_error("column count mismatch");

        for (std::size_t i = 0; i < doc.GetRowCount(); ++i) {
            Row row = columns | std::views::enumerate |
                      std::views::transform([&](const auto&& pair) -> Value {
                          auto&& [j, col] = pair;

                          switch (col.type) {
                              case ColumnType::STRING:
                                  return doc.GetCell<std::string>(j, i);
                              case ColumnType::INT:
                                  return doc.GetCell<int>(j, i);
                              case ColumnType::FLOAT:
                                  return doc.GetCell<double>(j, i);
                              case ColumnType::BOOL:
                                  return doc.GetCell<bool>(j, i);
                              default:
                                  std::unreachable();
                          }
                      }) |
                      std::ranges::to<Row>();

            std::println("inserting {}", row);
            table->insert(row);
        }
    }
}

void QueryExecutor::Executor::operator()(const parser::SelectStatement& stmt) {
    auto table = catalog.get_table(engine, stmt.table_name);
    if (!table) {
        std::println("table {} does not exist", stmt.table_name);
        return;
    }

    sink.on_columns(table->get_meta().columns);

    volcano::VolcanoIterator root{volcano::SeqScan{table->cursor()}};

    if (stmt.filter) {
        auto meta = table->get_meta();
        root = volcano::VolcanoIterator{
            volcano::Filter{std::move(root), *stmt.filter, std::move(meta)}
        };
    }

    while (auto row = root.next())
        sink.on_row(*row);
}

void QueryExecutor::Executor::operator()(const parser::InsertStatement& stmt) const {
    auto table = catalog.get_table(engine, stmt.table_name);
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
    auto table = catalog.get_table(engine, stmt.table_name);
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
