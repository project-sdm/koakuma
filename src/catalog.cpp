#include "catalog.hpp"
#include <cstddef>
#include <expected>
#include <filesystem>
#include <ranges>
#include <utility>
#include <variant>
#include "engine/file_manager.hpp"
#include "index/btree.hpp"
#include "index/hash.hpp"
#include "index/rtree.hpp"
#include "file/seq_file.hpp"
#include "util.hpp"

namespace catalog {

    IncompatibleColumnIndex::IncompatibleColumnIndex(IndexType index_type, ColumnType col_type)
        : index_type{index_type},
          col_type{col_type} {}

    DuplicatePrimaryKey::DuplicatePrimaryKey(Value pkey)
        : pkey{std::move(pkey)} {}

    Table::Table(SeqFile seq_file,
                 SeqFile::Meta meta,
                 std::unordered_map<std::string, std::size_t> col_nums,
                 std::unordered_map<std::size_t, AnyIndex> col_indices)
        : seq_file{seq_file},
          meta{std::move(meta)},
          col_nums{std::move(col_nums)},
          col_indices{std::move(col_indices)} {}

    const SeqFile::Meta& Table::get_meta() const {
        return meta;
    }

    SeqFile& Table::get_seq_file() {
        return seq_file;
    }

    std::size_t Table::col_num(const std::string& col_name) const {
        return col_nums.at(col_name);
    }

    std::size_t Table::pkey_col_num() const {
        return meta.pkey_col;
    }

    catalog::AnyIndex* Table::get_index(const std::string& col_name) {
        auto num = col_nums.at(col_name);
        auto it = col_indices.find(num);

        if (it == col_indices.end())
            return nullptr;

        return &it->second;
    }

    std::expected<Rid, catalog::InsertionError> Table::insert(const Row& row) {
        auto add_result = seq_file.add(row);
        if (!add_result)
            return std::unexpected{catalog::DuplicatePrimaryKey{row.at(meta.pkey_col)}};

        auto [rid, result] = *add_result;

        if (result == SeqFile::InsertResult::Rebuild) {
            auto cursor = seq_file.cursor();

            for (auto& [col_idx, index] : col_indices) {
                TRYV(std::visit(
                    util::overloaded{
                        [&](HashIndex& hash) -> std::expected<void, InsertionError> {
                            hash.init();

                            while (auto data = cursor.next()) {
                                auto [rid, row] = *data;
                                hash.add(TRY(val_to_hash_val(row[col_idx])), rid);
                            }

                            return {};
                        },
                        [&](BTreeIndex& btree) -> std::expected<void, InsertionError> {
                            btree.init();

                            while (auto data = cursor.next()) {
                                auto [rid, row] = *data;
                                btree.add(row[col_idx], rid);
                            }

                            return {};
                        },
                        [&](RTreeIndex<2>& rtree) -> std::expected<void, InsertionError> {
                            rtree.init();

                            while (auto data = cursor.next()) {
                                auto [rid, row] = *data;
                                auto point = std::get<Point<2>>(row[col_idx]);
                                rtree.add(point, rid);
                            }

                            return {};
                        }},
                    index));
            }
        } else {
            for (auto& [col_idx, index] : col_indices) {
                TRYV(std::visit(
                    util::overloaded{
                        [&](HashIndex& hash) -> std::expected<void, InsertionError> {
                            hash.add(TRY(val_to_hash_val(row[col_idx])), rid);
                            return {};
                        },
                        [&](BTreeIndex& btree) -> std::expected<void, InsertionError> {
                            btree.add(row[col_idx], rid);
                            return {};
                        },
                        [&](RTreeIndex<2>& rtree) -> std::expected<void, InsertionError> {
                            auto point = std::get<Point<2>>(row[col_idx]);
                            rtree.add(point, rid);
                            return {};
                        }},
                    index));
            }
        }

        return rid;
    }

    SeqFile::Cursor Table::cursor() {
        return seq_file.cursor();
    }

    Catalog::Catalog(std::filesystem::path data_path)
        : data_path{std::move(data_path)} {
        std::filesystem::create_directories(this->data_path);
    }

    std::filesystem::path Catalog::table_path(const std::string& name) const {
        return data_path / std::format("{}.table.bin", name);
    }

    std::filesystem::path Catalog::index_path(const std::string& table_name,
                                              const std::string& col_name) const {
        return data_path / std::format("{}.{}.index.bin", table_name, col_name);
    }

    std::expected<bool, CreateTableError> Catalog::create_table(Engine& eng,
                                                                const std::string& name,
                                                                std::vector<Column> columns,
                                                                std::size_t pkey_col) const {
        assert(pkey_col < columns.size());

        auto path = table_path(name);
        if (FileManager::exists(path))
            return false;

        for (const auto& col : columns) {
            if (col.index && *col.index == IndexType::RTree && col.type != ColumnType::Point2d) {
                return std::unexpected{
                    IncompatibleColumnIndex{*col.index, col.type}
                };
            }
        }

        for (const auto& col : columns) {
            if (col.index) {
                switch (*col.index) {
                    case IndexType::Hash:
                        create_index<HashIndex>(eng, name, col.name);
                        break;
                    case IndexType::BTree:
                        create_index<BTreeIndex>(eng, name, col.name);
                        break;
                    case IndexType::RTree:
                        create_index<RTreeIndex<2>>(eng, name, col.name);
                        break;
                }
            }
        }

        FileId fid = eng.file_mgr.open_create(path);

        {
            SeqFile seq_file{eng, fid};
            seq_file.init(std::move(columns), pkey_col);
        }

        eng.file_mgr.close(fid);
        return true;
    }

    std::optional<Table> Catalog::get_table(Engine& eng, const std::string& table_name) const {
        FileId fid = TRY_OPT(eng.file_mgr.open(table_path(table_name)));
        SeqFile seq_file{eng, fid};

        auto meta = seq_file.read_meta();
        const auto& cols = meta.columns;

        std::unordered_map<std::string, std::size_t> col_nums;
        std::unordered_map<std::size_t, AnyIndex> col_indices;

        for (const auto&& [i, col] : std::views::enumerate(cols)) {
            col_nums[col.name] = i;

            if (col.index) {
                FileId fid = TRY_OPT(eng.file_mgr.open(index_path(table_name, col.name)));

                switch (*col.index) {
                    case IndexType::Hash:
                        col_indices.emplace(i, HashIndex{eng, fid});
                        break;
                    case IndexType::BTree:
                        col_indices.emplace(i, BTreeIndex{eng, fid});
                        break;
                    case IndexType::RTree:
                        col_indices.emplace(i, RTreeIndex<2>{eng, fid});
                        break;
                }
            }
        }

        return Table{seq_file, std::move(meta), std::move(col_nums), std::move(col_indices)};
    }

    bool Catalog::drop_table(const std::string& name) const {
        return FileManager::remove(table_path(name));
    }

}  // namespace catalog
