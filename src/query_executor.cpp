#include "query_executor.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <expected>
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

UnexpectedType::UnexpectedType(parser::ExprLit lit, ColumnType expected_type)
    : lit{std::move(lit)},
      expected_type{expected_type} {}

TableNotFound::TableNotFound(std::string table_name)
    : table_name{std::move(table_name)} {}

TableAlreadyExists::TableAlreadyExists(std::string table_name)
    : table_name{std::move(table_name)} {}

InvalidCsvCell::InvalidCsvCell(std::string text, ColumnType expected_type)
    : text{std::move(text)},
      expected_type{expected_type} {}

namespace {
    std::expected<Value, ExecutionError> lit2val(parser::ExprLit lit, ColumnType col_type) {
        switch (col_type) {
            case ColumnType::INT:
                if (auto* x = std::get_if<f64>(&lit)) {
                    if (std::floor(*x) == *x)
                        return static_cast<int>(*x);
                }
                break;
            case ColumnType::FLOAT:
                if (auto* x = std::get_if<f64>(&lit))
                    return *x;

                break;
            case ColumnType::STRING:
                if (auto* s = std::get_if<std::string>(&lit))
                    return std::move(*s);

                break;
            case ColumnType::BOOL:
                if (auto* b = std::get_if<bool>(&lit))
                    return *b;

                break;
            default: {
            }
        }

        return std::unexpected{
            UnexpectedType{lit, col_type}
        };
    }

}  // namespace

namespace volcano {

    template<typename T>
    concept volcano_iterator = util::iter_of<T, Row>;

    class VolcanoIterator {
        class Contract;

    public:
        using value_type = Row;

        std::optional<value_type> next() {
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

    template<util::iter_of<std::pair<Rid, Row>> Iter>
    class SeqScan {
    public:
        using value_type = Row;

        std::optional<value_type> next() {
            return TRY_OPT(it.next()).second;
        }

        explicit SeqScan(Iter it)
            : it{std::move(it)} {}

    private:
        Iter it;
    };

    class PKeyScan {
    public:
        using value_type = Row;

        PKeyScan(SeqFile& seq_file, Value search_pkey)
            : seq_file{seq_file},
              search_pkey{std::move(search_pkey)} {}

        std::optional<value_type> next() {
            auto pkey = TRY_OPT(std::exchange(search_pkey, std::nullopt));
            return seq_file.search(pkey);
        }

    private:
        SeqFile& seq_file;
        std::optional<Value> search_pkey;
    };

    static_assert(volcano_iterator<PKeyScan>);

    template<util::iter_of<Rid> Iter>
    class IndexScan {
    public:
        using value_type = Row;

        explicit IndexScan(SeqFile& seq_file, Iter it)
            : seq_file{seq_file},
              it{std::move(it)} {}

        std::optional<value_type> next() {
            Rid rid = TRY_OPT(it.next());
            return seq_file.read_rid(rid);
        }

    private:
        SeqFile& seq_file;
        Iter it;
    };

    class FilterVisitor {
    public:
        FilterVisitor(ColumnType lhs_type, Value data)
            : lhs_type{lhs_type},
              data{std::move(data)} {}

        std::expected<bool, ExecutionError> operator()(const parser::EqFilter& eq_filter) {
            return data == TRY(lit2val(eq_filter.value, lhs_type));
        }

        std::expected<bool, ExecutionError> operator()(const parser::RangeFilter& range_filter) {
            auto low = TRY(lit2val(range_filter.low, lhs_type));
            auto high = TRY(lit2val(range_filter.high, lhs_type));

            return low <= data && data <= high;
        }

        std::expected<bool, ExecutionError> operator()(
            [[maybe_unused]] const parser::RadFilter& rad_filter) {
            assert(false && "TODO: implement");
        }

        std::expected<bool, ExecutionError> operator()(
            [[maybe_unused]] const parser::KFilter& k_filter) {
            assert(false && "TODO: implement");
        }

    private:
        ColumnType lhs_type;
        Value data;
    };

    class Filter {
    public:
        using value_type = Row;

        Filter(VolcanoIterator child, parser::Filter filter, SeqFile::Meta meta)
            : child{std::move(child)},
              meta{std::move(meta)},
              data{std::move(filter.data)} {
            for (const auto&& [i, col] : std::views::enumerate(this->meta.columns)) {
                if (col.name == filter.col_name)
                    col_num = i;
            }
        }

        std::optional<Row> next() {
            const auto& col = meta.columns.at(col_num);

            while (auto row = child.next()) {
                FilterVisitor visitor{col.type, row->at(col_num)};
                if (std::visit(visitor, data))
                    return row;
            }

            return std::nullopt;
        }

    private:
        VolcanoIterator child;
        SeqFile::Meta meta;
        parser::FilterData data;
        std::size_t col_num{};
    };

    static_assert(volcano_iterator<Filter>);

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
            case DataType::POINT2D:
                return ColumnType::POINT;
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

std::expected<u32, ExecutionError> QueryExecutor::exec(const catalog::Catalog& catalog,
                                                       const parser::SourceFile& source_file,
                                                       RowSink& sink) {
    Executor executor{engine, catalog, sink};

    for (const auto& stmt : source_file.statements) {
        std::println("==================================");
        TRYV(std::visit(executor, stmt));
        std::println();
    }

    return static_cast<u32>(source_file.statements.size());
}

std::expected<void, ExecutionError> QueryExecutor::Executor::operator()(
    const parser::CreateStatement& stmt) const {
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
        if (!stmt.if_not_exists)
            return std::unexpected{TableAlreadyExists{stmt.table_name}};

        std::println("warning: {}", TableAlreadyExists{stmt.table_name});
        return {};
    }

    if (stmt.file_path) {
        // load from csv
        auto table = catalog.get_table(eng, stmt.table_name);
        assert(table);

        rapidcsv::Document doc{*stmt.file_path};

        if (doc.GetColumnCount() != columns.size())
            throw std::runtime_error("column count mismatch");

        for (std::size_t i = 0; i < doc.GetRowCount(); ++i) {
            using Result = std::expected<Value, InvalidCsvCell>;

            static auto has_error = [](const Result& e) { return !e.has_value(); };
            static auto get_value = [](const Result&& e) { return e.value(); };

            auto row_results = columns | std::views::enumerate |
                               std::views::transform([&](const auto&& pair) -> Result {
                                   auto&& [j, col] = pair;

                                   try {
                                       switch (col.type) {
                                           case ColumnType::STRING:
                                               return doc.GetCell<std::string>(j, i);
                                           case ColumnType::INT:
                                               return doc.GetCell<int>(j, i);
                                           case ColumnType::FLOAT:
                                               return doc.GetCell<double>(j, i);
                                           case ColumnType::BOOL:
                                               return doc.GetCell<bool>(j, i);
                                           case ColumnType::POINT:
                                               return doc.GetCell<parser::Point2D>(j, i);
                                       }

                                       std::unreachable();
                                   } catch (...) {
                                       auto text = doc.GetCell<std::string>(j, i);
                                       return std::unexpected{
                                           InvalidCsvCell{text, col.type}
                                       };
                                   }
                               });

            if (auto v = std::ranges::find_if(row_results, has_error); v != row_results.end())
                return std::unexpected{(*v).error()};

            Row row =
                row_results | std::views::transform(get_value) | std::ranges::to<std::vector>();

            TRYV(table->insert(row));
        }
    }

    return {};
}

std::expected<volcano::VolcanoIterator, ExecutionError> QueryExecutor::Executor::apply_filter(
    volcano::VolcanoIterator iter,
    catalog::Table& table,
    const parser::Filter& filter) {
    auto meta = table.get_meta();
    std::size_t col_num = table.col_num(filter.col_name);
    auto col = meta.columns.at(col_num);

    if (col_num == table.pkey_col_num()) {
        if (const auto* eq_filter = std::get_if<parser::EqFilter>(&filter.data)) {
            std::println("using pkey");
            auto val = TRY(lit2val(eq_filter->value, col.type));

            volcano::PKeyScan scan{table.get_seq_file(), std::move(val)};
            return volcano::VolcanoIterator{std::move(scan)};
        }

        if (const auto* range_filter = std::get_if<parser::RangeFilter>(&filter.data)) {
            std::println("using pkey range");
            auto low = TRY(lit2val(range_filter->low, col.type));
            auto high = TRY(lit2val(range_filter->high, col.type));

            volcano::SeqScan scan{table.get_seq_file().range_search(low, high)};
            return volcano::VolcanoIterator{std::move(scan)};
        }
    }

    if (auto* index = table.get_index(filter.col_name)) {
        if (const auto* eq_filter = std::get_if<parser::EqFilter>(&filter.data)) {
            if (col_num == table.pkey_col_num()) {
                std::println("using pkey");
                auto val = TRY(lit2val(eq_filter->value, col.type));

                volcano::PKeyScan scan{table.get_seq_file(), std::move(val)};
                return volcano::VolcanoIterator{std::move(scan)};
            }

            if (auto* hash_index = std::get_if<HashIndex>(index)) {
                std::println("using eq hash");
                auto val = TRY(lit2val(eq_filter->value, col.type));
                auto hash_val = TRY(val_to_hash_val(std::move(val)));
                auto cursor = hash_index->search(hash_val);

                volcano::IndexScan scan{table.get_seq_file(), std::move(cursor)};
                return volcano::VolcanoIterator{std::move(scan)};
            }

            if (auto* btree_index = std::get_if<BTreeIndex>(index)) {
                std::println("using eq btree");
                auto val = TRY(lit2val(eq_filter->value, col.type));
                auto cursor = btree_index->search(val);

                volcano::IndexScan scan{table.get_seq_file(), std::move(cursor)};
                return volcano::VolcanoIterator{std::move(scan)};
            }
        }

        if (const auto* range_filter = std::get_if<parser::RangeFilter>(&filter.data)) {
            if (auto* btree_index = std::get_if<BTreeIndex>(index)) {
                std::println("using btree range");

                auto low = TRY(lit2val(range_filter->low, col.type));
                auto high = TRY(lit2val(range_filter->high, col.type));

                auto& seq_file = table.get_seq_file();
                auto cursor = btree_index->range_search(low, high);

                volcano::IndexScan scan{seq_file, std::move(cursor)};
                return volcano::VolcanoIterator{std::move(scan)};
            }
        }
    }

    return volcano::VolcanoIterator{
        volcano::Filter{std::move(iter), filter, std::move(meta)}
    };
}

std::expected<void, ExecutionError> QueryExecutor::Executor::operator()(
    const parser::SelectStatement& stmt) {
    auto table = catalog.get_table(eng, stmt.table_name);

    if (!table)
        return std::unexpected{TableNotFound{stmt.table_name}};

    const auto& cols = table->get_meta().columns;
    sink.on_columns(cols);

    volcano::VolcanoIterator root{volcano::SeqScan{table->cursor()}};

    if (stmt.filter)
        root = TRY(apply_filter(std::move(root), *table, *stmt.filter));

    while (auto row = root.next())
        sink.on_row(*row);

    return {};
}

std::expected<void, ExecutionError> QueryExecutor::Executor::operator()(
    const parser::InsertStatement& stmt) const {
    auto table = catalog.get_table(eng, stmt.table_name);
    if (!table) {
        std::println("table {} does not exist", stmt.table_name);
        return {};
    }

    for (const auto& value : stmt.values) {
        Row row = map_exprs(value.exprs);
        TRYV(table->insert(row));
    }

    return {};
}

std::expected<void, ExecutionError> QueryExecutor::Executor::operator()(
    const parser::DeleteStatement& stmt) const {
    auto table = catalog.get_table(eng, stmt.table_name);
    if (!table)
        return std::unexpected{TableNotFound{stmt.table_name}};

    auto cursor = table->cursor();

    while (auto row = cursor.next()) {
        if (!stmt.filter.has_value() /* || applies(row, *stmt.filter) */)
            return {};  // TODO: delete
    }

    return {};
}

template<>
void rapidcsv::Converter<bool>::ToVal(const std::string& pStr, bool& pVal) const {
    if (pStr == "true")
        pVal = true;
    else if (pStr == "false")
        pVal = false;
    else
        throw std::invalid_argument("invalid bool");
}

template<>
void rapidcsv::Converter<parser::Point2D>::ToVal(const std::string& pStr,
                                                 parser::Point2D& pVal) const {
    std::size_t idx = 0;

    pVal.x = std::stod(pStr, &idx);

    if (pStr[idx] != ';')
        throw std::invalid_argument("no ; in csv point");

    pVal.y = std::stod(pStr.substr(idx + 1), &idx);
}
