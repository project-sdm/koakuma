#include "query_executor.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <print>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "catalog.hpp"
#include "engine/engine.hpp"
#include "file/common.hpp"
#include "file/seq_file.hpp"
#include "index/hash.hpp"
#include "parser/ast.hpp"
#include "parser/token.hpp"
#include "rapidcsv/rapidcsv.h"
#include "util.hpp"

UnexpectedType::UnexpectedType(parser::ExprLit lit, ColumnType expected_type)
    : lit{std::move(lit)},
      expected_type{expected_type} {}

TableNotFound::TableNotFound(std::string table_name)
    : table_name{std::move(table_name)} {}

ColumnNotFound::ColumnNotFound(std::string table_name, std::string col_name)
    : table_name{std::move(table_name)},
      col_name{std::move(col_name)} {}

TableAlreadyExists::TableAlreadyExists(std::string table_name)
    : table_name{std::move(table_name)} {}

InvalidCsvCell::InvalidCsvCell(std::string text, ColumnType expected_type)
    : text{std::move(text)},
      expected_type{expected_type} {}

FileNotFound::FileNotFound(std::filesystem::path path)
    : path{std::move(path)} {}

InvalidIndexName::InvalidIndexName(std::string name)
    : name{std::move(name)} {}

UnsupportedOperation::UnsupportedOperation(std::string col_name)
    : col_name{std::move(col_name)} {}

namespace {
    std::expected<Value, ExecutionError> lit2val(parser::ExprLit lit, ColumnType col_type) {
        switch (col_type) {
            case ColumnType::Int:
                if (auto* x = std::get_if<f64>(&lit)) {
                    if (std::floor(*x) == *x)
                        return static_cast<int>(*x);
                }
                break;
            case ColumnType::Real:
                if (auto* x = std::get_if<f64>(&lit))
                    return *x;

                break;
            case ColumnType::VarChar:
                if (auto* s = std::get_if<std::string>(&lit))
                    return std::move(*s);

                break;
            case ColumnType::Bool:
                if (auto* b = std::get_if<bool>(&lit))
                    return *b;

                break;
            case ColumnType::Point2d:
                if (auto* p = std::get_if<parser::Point2D>(&lit))
                    return Point<2>{p->x, p->y};

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
    concept volcano_iterator = requires(T it) {
        { it.next() } -> std::same_as<std::expected<std::optional<Row>, ExecutionError>>;
    };

    class VolcanoIterator {
        class Contract;

    public:
        std::expected<std::optional<Row>, ExecutionError> next() {
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

            virtual std::expected<std::optional<Row>, ExecutionError> next() = 0;

            virtual ~Contract() = default;
        };

        template<volcano_iterator It>
        class Wrapper final : public Contract {
        public:
            std::expected<std::optional<Row>, ExecutionError> next() override {
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
        explicit SeqScan(Iter it)
            : it{std::move(it)} {}

        std::expected<std::optional<Row>, ExecutionError> next() {
            return TRY_OPT(it.next()).second;
        }

    private:
        Iter it;
    };

    class PKeyScan {
    public:
        PKeyScan(SeqFile& seq_file, Value search_pkey)
            : seq_file{seq_file},
              search_pkey{std::move(search_pkey)} {}

        std::expected<std::optional<Row>, ExecutionError> next() {
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
        explicit IndexScan(SeqFile& seq_file, Iter it)
            : seq_file{seq_file},
              it{std::move(it)} {}

        std::expected<std::optional<Row>, ExecutionError> next() {
            Rid rid = TRY_OPT(it.next());
            return seq_file.read_rid(rid);
        }

    private:
        SeqFile& seq_file;
        Iter it;
    };

    class Filter {
    public:
        Filter(VolcanoIterator child, parser::Filter filter, SeqFile::Meta meta)
            : child{std::move(child)},
              meta{std::move(meta)},
              data{std::move(filter.data)} {
            for (const auto&& [i, col] : std::views::enumerate(this->meta.columns)) {
                if (col.name == filter.col_name)
                    col_num = i;
            }
        }

        std::expected<std::optional<Row>, ExecutionError> next() {
            const auto& col = meta.columns.at(col_num);

            while (auto row = TRY(child.next())) {
                auto val = row->at(col_num);

                bool cond = TRY(std::visit(
                    util::overloaded{
                        [&](const parser::EqFilter& eq_filter)
                            -> std::expected<bool, ExecutionError> {
                            return val == TRY(lit2val(eq_filter.value, col.type));
                        },
                        [&](const parser::RangeFilter& range_filter)
                            -> std::expected<bool, ExecutionError> {
                            auto low = TRY(lit2val(range_filter.low, col.type));
                            auto high = TRY(lit2val(range_filter.high, col.type));

                            return low <= val && val <= high;
                        },
                        [&](const parser::RadFilter&) -> std::expected<bool, ExecutionError> {
                            return std::unexpected{UnsupportedOperation{col.name}};
                        },
                        [&](const parser::KFilter&) -> std::expected<bool, ExecutionError> {
                            return std::unexpected{UnsupportedOperation{col.name}};
                        }},
                    data));

                if (cond)
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
            case DataType::Bool:
                return ColumnType::Bool;
            case DataType::VarChar:
                return ColumnType::VarChar;
            case DataType::Int:
                return ColumnType::Int;
            case DataType::Real:
                return ColumnType::Real;
            case DataType::Point2d:
                return ColumnType::Point2d;
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

std::expected<void, ExecutionError> QueryExecutor::exec(const catalog::Catalog& catalog,
                                                        const parser::SourceFile& source_file,
                                                        RowSink& sink) {
    Executor executor{engine, catalog, sink};

    for (const auto& stmt : source_file.statements) {
        sink.on_begin();
        TRYV(std::visit(executor, stmt));
        std::println();
    }

    return {};
}

QueryExecutor::Executor::Executor(Engine& eng, const catalog::Catalog& catalog, RowSink& sink)
    : eng{eng},
      catalog{catalog},
      sink{sink} {}

std::expected<void, ExecutionError> QueryExecutor::Executor::operator()(
    const parser::CreateStatement& stmt) const {
    if (stmt.file_path && !std::filesystem::exists(*stmt.file_path))
        return std::unexpected{FileNotFound{*stmt.file_path}};

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
                    col_index = IndexType::Hash;
                else if (index->name == "btree")
                    col_index = IndexType::BTree;
                else if (index->name == "rtree")
                    col_index = IndexType::RTree;
                else
                    return std::unexpected{InvalidIndexName{index->name}};
            }
        }

        columns.emplace_back(col.name, get_col_type(col.type), col_index);
    }

    bool created = TRY(catalog.create_table(eng, stmt.table_name, columns, pkey_col));

    if (!created) {
        if (!stmt.if_not_exists)
            return std::unexpected{TableAlreadyExists{stmt.table_name}};

        sink.on_warning(std::format("{}", TableAlreadyExists{stmt.table_name}));
        return {};
    }

    if (stmt.file_path) {
        // load from csv
        auto table = catalog.get_table(eng, stmt.table_name);
        assert(table);

        rapidcsv::Document doc{*stmt.file_path};

        if (doc.GetColumnCount() != columns.size())
            return std::unexpected{InvalidCsvSchema{}};

        for (std::size_t i = 0; i < doc.GetRowCount(); ++i) {
            using Result = std::expected<Value, InvalidCsvCell>;

            static auto has_error = [](const Result& e) { return !e.has_value(); };
            static auto get_value = [](const Result&& e) { return e.value(); };

            auto row_results = columns | std::views::enumerate |
                               std::views::transform([&](const auto&& pair) -> Result {
                                   auto&& [j, col] = pair;

                                   try {
                                       switch (col.type) {
                                           case ColumnType::VarChar:
                                               return doc.GetCell<std::string>(j, i);
                                           case ColumnType::Int:
                                               return doc.GetCell<int>(j, i);
                                           case ColumnType::Real:
                                               return doc.GetCell<double>(j, i);
                                           case ColumnType::Bool:
                                               return doc.GetCell<bool>(j, i);
                                           case ColumnType::Point2d:
                                               return doc.GetCell<Point<2>>(j, i);
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

std::expected<void, ExecutionError> QueryExecutor::Executor::operator()(
    const parser::DropStatement& stmt) const {
    if (!catalog.drop_table(stmt.table_name)) {
        if (!stmt.if_exists)
            return std::unexpected{TableNotFound{stmt.table_name}};

        sink.on_warning(std::format("{}", TableNotFound{stmt.table_name}));
        return {};
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

            if (auto* hash = std::get_if<HashIndex>(index)) {
                std::println("using eq hash");
                auto val = TRY(lit2val(eq_filter->value, col.type));
                auto hash_val = TRY(val_to_hash_val(std::move(val)));
                auto cursor = hash->search(hash_val);

                volcano::IndexScan scan{table.get_seq_file(), std::move(cursor)};
                return volcano::VolcanoIterator{std::move(scan)};
            }

            if (auto* btree = std::get_if<BTreeIndex>(index)) {
                std::println("using eq btree");
                auto val = TRY(lit2val(eq_filter->value, col.type));
                auto cursor = btree->search(val);

                volcano::IndexScan scan{table.get_seq_file(), std::move(cursor)};
                return volcano::VolcanoIterator{std::move(scan)};
            }
        }

        if (const auto* range_filter = std::get_if<parser::RangeFilter>(&filter.data)) {
            if (auto* btree = std::get_if<BTreeIndex>(index)) {
                std::println("using btree range");

                auto low = TRY(lit2val(range_filter->low, col.type));
                auto high = TRY(lit2val(range_filter->high, col.type));

                auto& seq_file = table.get_seq_file();
                auto cursor = btree->range_search(low, high);

                volcano::IndexScan scan{seq_file, std::move(cursor)};
                return volcano::VolcanoIterator{std::move(scan)};
            }
        }

        if (const auto* rad_filter = std::get_if<parser::RadFilter>(&filter.data)) {
            if (auto* rtree = std::get_if<RTreeIndex<2>>(index)) {
                std::println("using rtree radius search");

                Point<2> point{rad_filter->origin.x, rad_filter->origin.y};

                auto& seq_file = table.get_seq_file();
                auto cursor = rtree->range_search(point, rad_filter->radius);

                volcano::IndexScan scan{seq_file, std::move(cursor)};
                return volcano::VolcanoIterator{std::move(scan)};
            }
        }

        if (const auto* k_filter = std::get_if<parser::KFilter>(&filter.data)) {
            if (auto* rtree = std::get_if<RTreeIndex<2>>(index)) {
                std::println("using rtree knn");

                Point<2> point{k_filter->origin.x, k_filter->origin.y};

                auto& seq_file = table.get_seq_file();
                auto cursor = rtree->knn(point, k_filter->k);

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
    sink.on_table(cols);

    volcano::VolcanoIterator root{volcano::SeqScan{table->cursor()}};

    if (stmt.filter)
        root = TRY(apply_filter(std::move(root), *table, *stmt.filter));

    while (auto row = TRY(root.next()))
        sink.on_row(*row);

    return {};
}

std::expected<void, ExecutionError> QueryExecutor::Executor::operator()(
    const parser::InsertStatement& stmt) const {
    auto table = catalog.get_table(eng, stmt.table_name);
    if (!table)
        return std::unexpected{TableNotFound{stmt.table_name}};

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

std::expected<void, ExecutionError> QueryExecutor::Executor::operator()(
    const parser::ShowStatement& stmt) const {
    auto table = catalog.get_table(eng, stmt.table_name);
    if (!table)
        return std::unexpected{TableNotFound{stmt.table_name}};

    const auto& cols = table->get_meta().columns;

    if (!std::ranges::any_of(cols, [&](auto& col) { return col.name == stmt.col_name; })) {
        return std::unexpected{
            ColumnNotFound{stmt.table_name, stmt.col_name}
        };
    }

    if (auto* index = table->get_index(stmt.col_name)) {
        if (auto* rtree = std::get_if<RTreeIndex<2>>(index)) {
            auto cursor = rtree->rect_cursor();

            sink.on_plane();

            while (auto entry = cursor.next()) {
                auto [level, rect] = std::move(*entry);
                sink.on_rect(level, rect);
            }

            return {};
        }
    }

    return std::unexpected{UnsupportedOperation{stmt.col_name}};
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
void rapidcsv::Converter<Point<2>>::ToVal(const std::string& pStr, Point<2>& pVal) const {
    std::size_t idx = 0;

    pVal[0] = std::stod(pStr, &idx);

    if (pStr[idx] != ';')
        throw std::invalid_argument("no ; in csv point");

    pVal[1] = std::stod(pStr.substr(idx + 1), &idx);
}
