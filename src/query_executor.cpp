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
#include "index/btree.hpp"
#include "index/hash.hpp"
#include "parser/ast.hpp"
#include "parser/token.hpp"
#include "rapidcsv/rapidcsv.h"
#include "seq_file.hpp"
#include "util.hpp"

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

    class PKeyScan {
    public:
        PKeyScan(SeqFile& seq_file, Value search_pkey)
            : seq_file{seq_file},
              search_pkey{std::move(search_pkey)} {}

        std::optional<Row> next() {
            auto pkey = TRY_OPT(std::exchange(search_pkey, std::nullopt));
            return seq_file.search(pkey);
        }

    private:
        SeqFile& seq_file;
        std::optional<Value> search_pkey;
    };

    template<util::iter_of<Rid> Iter>
    class IndexScan {
    public:
        explicit IndexScan(SeqFile& seq_file, Iter it)
            : seq_file{seq_file},
              it{std::move(it)} {}

        std::optional<Row> next() {
            Rid rid = TRY_OPT(it.next());
            return seq_file.read_rid(rid);
        }

    private:
        SeqFile& seq_file;
        Iter it;
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
                if (std::visit(FilterVisitor{data}, row->at(col_num)))
                    return row;
            }

            return std::nullopt;
        }

        Filter(VolcanoIterator child, parser::Filter filter, SeqFile::Meta meta)
            : child{std::move(child)},
              meta{std::move(meta)},
              data{std::move(filter.data)} {
            for (const auto&& [i, col] : std::views::enumerate(this->meta.columns)) {
                if (col.name == filter.col_identifier)
                    col_num = i;
            }
        }

    private:
        VolcanoIterator child;
        SeqFile::Meta meta;
        parser::FilterData data;
        std::size_t col_num{};
    };

}  // namespace volcano

namespace {

    ColumnType get_col_type(DataType col) {
        switch (col) {
            case DataType::BOOL:
                return ColumnType::BOOL;
            case DataType::VARCHAR:
                return ColumnType::STRING;
            case DataType::INT:
                return ColumnType::INT;
            case DataType::REAL:
                return ColumnType::FLOAT;
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

    bool created = catalog.create_table(eng, stmt.table_name, columns, pkey_col);

    if (!created) {
        std::println("table {} already exists", stmt.table_name);
        return;
    }

    if (stmt.file_path) {
        // load from csv
        auto table = catalog.get_table(eng, stmt.table_name);
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
                          }

                          std::unreachable();
                      }) |
                      std::ranges::to<Row>();

            std::println("inserting {}", row);
            table->insert(row);
        }
    }
}

void QueryExecutor::Executor::operator()(const parser::SelectStatement& stmt) {
    auto table = catalog.get_table(eng, stmt.table_name);
    if (!table) {
        std::println("table {} does not exist", stmt.table_name);
        return;
    }

    const auto& cols = table->get_meta().columns;
    sink.on_columns(cols);

    volcano::VolcanoIterator root{volcano::SeqScan{table->cursor()}};

    if (stmt.filter) {
        std::size_t col_num = table->col_num(stmt.filter->col_identifier);
        auto col = cols.at(col_num);

        if (col_num == table->pkey_col_num()) {
            if (const auto* eq_filter = std::get_if<parser::EqFilter>(&stmt.filter->data)) {
                std::println("using pkey");
                auto val = lit2val(eq_filter->value, col.type);

                root = volcano::VolcanoIterator{
                    volcano::PKeyScan{table->get_seq_file(), std::move(val)}
                };

                goto skip;
            }
        } else if (auto* index = table->get_index(stmt.filter->col_identifier)) {
            if (const auto* eq_filter = std::get_if<parser::EqFilter>(&stmt.filter->data)) {
                if (col_num == table->pkey_col_num()) {
                    std::println("using pkey");
                    auto val = lit2val(eq_filter->value, col.type);

                    root = volcano::VolcanoIterator{
                        volcano::PKeyScan{table->get_seq_file(), std::move(val)}
                    };
                } else if (auto* hash_index = std::get_if<HashIndex>(index)) {
                    std::println("using eq hash");
                    auto val = lit2val(eq_filter->value, col.type);
                    auto hash_val = val_to_hash_val(val);
                    auto cursor = hash_index->search(*hash_val);

                    root = volcano::VolcanoIterator{
                        volcano::IndexScan{table->get_seq_file(), std::move(cursor)}
                    };
                } else if (auto* btree_index = std::get_if<BTreeIndex>(index)) {
                    std::println("using eq btree");
                    auto val = lit2val(eq_filter->value, col.type);
                    auto cursor = btree_index->search(val);

                    root = volcano::VolcanoIterator{
                        volcano::IndexScan{table->get_seq_file(), std::move(cursor)}
                    };
                } else {
                    std::unreachable();
                }

                goto skip;
            } else if (const auto* range_filter =
                           std::get_if<parser::RangeFilter>(&stmt.filter->data)) {
                if (auto* btree_index = std::get_if<BTreeIndex>(index)) {
                    std::println("using btree range");

                    auto low = lit2val(range_filter->min_val, col.type);
                    auto high = lit2val(range_filter->max_val, col.type);

                    auto& seq_file = table->get_seq_file();
                    auto cursor = btree_index->range_search(low, high);

                    root = volcano::VolcanoIterator{
                        volcano::IndexScan{seq_file, std::move(cursor)}
                    };
                    goto skip;
                }
            }
        }

        auto meta = table->get_meta();

        root = volcano::VolcanoIterator{
            volcano::Filter{std::move(root), *stmt.filter, std::move(meta)}
        };
    }
skip:

    while (auto row = root.next())
        sink.on_row(*row);
}

void QueryExecutor::Executor::operator()(const parser::InsertStatement& stmt) const {
    auto table = catalog.get_table(eng, stmt.table_name);
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
    auto table = catalog.get_table(eng, stmt.table_name);
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
