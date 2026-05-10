#include "catalog.hpp"
#include <cassert>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <variant>
#include "engine/file_manager.hpp"
#include "file/append_file.hpp"
#include "file/common.hpp"
#include "file/seq_file.hpp"
#include "index/btree.hpp"
#include "index/hash.hpp"
#include "index/rtree.hpp"
#include "util.hpp"

namespace catalog {

    IncompatibleColumnIndex::IncompatibleColumnIndex(IndexType index_type, ColumnType col_type)
        : index_type{index_type},
          col_type{col_type} {}

    DuplicatePrimaryKey::DuplicatePrimaryKey(Value pkey)
        : pkey{std::move(pkey)} {}

    Table::Table(AnyFile file,
                 UnknownFile::Header meta,
                 std::unordered_map<std::string, std::size_t> col_nums,
                 std::unordered_map<std::size_t, AnyIndex> col_indices)
        : file{std::move(file)},
          meta{std::move(meta)},
          col_nums{std::move(col_nums)},
          col_indices{std::move(col_indices)} {}

    const UnknownFile::Header& Table::get_meta() const {
        return meta;
    }

    SeqFile* Table::as_seq_file() {
        return std::get_if<SeqFile>(&file);
    }

    Row Table::read_rid(Rid rid) {
        return std::visit([&](auto&& file) { return file.read_rid(rid); }, file);
    }

    std::size_t Table::col_num(const std::string& col_name) const {
        return col_nums.at(col_name);
    }

    std::expected<void, catalog::InsertionError> Table::rebuild_indices() {
        for (auto& [col_idx, index] : col_indices) {
            TRYV(std::visit(
                util::overloaded{[&](HashIndex& hash) -> std::expected<void, InsertionError> {
                                     hash.init();

                                     TRYV(std::visit(
                                         [&](auto&& file) -> std::expected<void, InsertionError> {
                                             auto cursor = file.cursor();

                                             while (auto data = cursor.next()) {
                                                 auto [rid, row] = *data;
                                                 hash.add(TRY(val_to_hash_val(row[col_idx])), rid);
                                             }

                                             return {};
                                         },
                                         file));

                                     return {};
                                 },
                                 [&](BTreeIndex& btree) -> std::expected<void, InsertionError> {
                                     btree.init();

                                     std::visit(
                                         [&](auto&& file) {
                                             auto cursor = file.cursor();

                                             while (auto data = cursor.next()) {
                                                 auto [rid, row] = *data;
                                                 btree.add(row[col_idx], rid);
                                             }
                                         },
                                         file);

                                     return {};
                                 },
                                 [&](RTreeIndex<2>& rtree) -> std::expected<void, InsertionError> {
                                     rtree.init();

                                     std::visit(
                                         [&](auto&& file) {
                                             auto cursor = file.cursor();
                                             while (auto data = cursor.next()) {
                                                 auto [rid, row] = *data;
                                                 auto point = std::get<Point<2>>(row[col_idx]);
                                                 rtree.add(point, rid);
                                             }
                                         },
                                         file);

                                     return {};
                                 }},
                index));
        }

        return {};
    }

    catalog::AnyIndex* Table::get_index(const std::string& col_name) {
        auto num = col_nums.at(col_name);
        auto it = col_indices.find(num);

        if (it == col_indices.end())
            return nullptr;

        return &it->second;
    }

    std::expected<Rid, catalog::InsertionError> Table::insert(const Row& row) {
        Rid rid;

        if (auto* seq_file = std::get_if<SeqFile>(&file)) {
            auto add_result = seq_file->add(row);
            if (!add_result)
                return std::unexpected{
                    catalog::DuplicatePrimaryKey{row.at(seq_file->pkey_col_num())}};

            auto [got_rid, result] = *add_result;

            if (result == SeqFile::InsertResult::Rebuild) {
                TRYV(rebuild_indices());
                return got_rid;
            }

            rid = got_rid;
        } else if (auto* append_file = std::get_if<AppendFile>(&file)) {
            rid = append_file->add(row);
        } else {
            std::unreachable();
        }

        for (auto& [col_idx, index] : col_indices) {
            TRYV(std::visit(
                util::overloaded{[&](HashIndex& hash) -> std::expected<void, InsertionError> {
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

        return rid;
    }

    std::expected<bool, ValueNotHashable> Table::delete_by_pkey(const Value& pkey) {
        auto* seq_file = std::get_if<SeqFile>(&file);
        if (seq_file == nullptr)
            throw std::runtime_error("cannot remove from AppendFile");

        auto res = seq_file->remove(pkey);
        if (!res)
            return false;

        auto [rid, row] = *res;

        for (auto& [col_idx, index] : col_indices) {
            using Result = std::expected<bool, ValueNotHashable>;

            TRYV(std::visit(util::overloaded{[&](HashIndex& hash) -> Result {
                                                 auto hash_val = TRY(val_to_hash_val(row[col_idx]));
                                                 assert(hash.remove(hash_val, rid));
                                                 return {};
                                             },
                                             [&](BTreeIndex& btree) -> Result {
                                                 assert(btree.remove(row[col_idx], rid));
                                                 return {};
                                             },
                                             [&](RTreeIndex<2>& rtree) -> Result {
                                                 auto point = std::get<Point<2>>(row[col_idx]);
                                                 assert(rtree.remove(point, rid));
                                                 return {};
                                             }},
                            index));
        }

        return true;
    }

    FileCursor::FileCursor(AnyCursor cursor)
        : cursor{std::move(cursor)} {}

    FileCursor Table::cursor() {
        return std::visit([&](auto&& file) { return FileCursor{file.cursor()}; }, file);
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

    std::expected<bool, CreateTableError> Catalog::create_table(
        Engine& eng,
        const std::string& name,
        const std::vector<Column>& columns,
        std::optional<std::size_t> pkey_col) const {
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

        if (pkey_col) {
            SeqFile seq_file{eng, fid};
            seq_file.init(columns, *pkey_col);
        } else {
            AppendFile append_file{eng, fid};
            append_file.init(columns);
        }

        eng.file_mgr.close(fid);
        return true;
    }

    std::optional<Table> Catalog::get_table(Engine& eng, const std::string& table_name) const {
        FileId fid = TRY_OPT(eng.file_mgr.open(table_path(table_name)));

        auto hdr = eng.file_mgr.read_user_header<UnknownFile::Header>(fid);
        const auto& cols = hdr.columns;

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

        switch (hdr.file_type) {
            case FileType::Seq: {
                return Table{
                    SeqFile{eng, fid},
                    std::move(hdr),
                    std::move(col_nums),
                    std::move(col_indices),
                };
            }
            case FileType::Append: {
                return Table{
                    AppendFile{eng, fid},
                    std::move(hdr), std::move(col_nums),
                    std::move(col_indices)
                };
            }
        }

        std::unreachable();
    }

    bool Catalog::drop_table(const std::string& name) const {
        return FileManager::remove(table_path(name));
    }

}  // namespace catalog
