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
#include "seq_file.hpp"
#include "util.hpp"

catalog::DuplicatePrimaryKey::DuplicatePrimaryKey(Value pkey)
    : pkey{std::move(pkey)} {}

using Table = catalog::Table;

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
    // TODO: rebuild indices on seq_file rebuild
    auto add_result = seq_file.add(row);
    if (!add_result)
        return std::unexpected{catalog::DuplicatePrimaryKey{row.at(meta.pkey_col)}};

    auto [rid, result] = *add_result;

    if (result == SeqFile::InsertResult::REBUILD) {
        auto cursor = seq_file.cursor();

        for (auto& [col_idx, index] : col_indices) {
            if (auto* hash = std::get_if<HashIndex>(&index)) {
                hash->init();

                while (auto data = cursor.next()) {
                    auto [rid, row] = *data;
                    hash->add(TRY(val_to_hash_val(row[col_idx])), rid);
                }
            } else if (auto* btree = std::get_if<BTreeIndex>(&index)) {
                btree->init();

                while (auto data = cursor.next()) {
                    auto [rid, row] = *data;
                    btree->add(row[col_idx], rid);
                }
            } else {
                std::unreachable();
            }
        }
    } else {
        for (auto& [col_idx, index] : col_indices) {
            if (auto* hash = std::get_if<HashIndex>(&index))
                hash->add(TRY(val_to_hash_val(row[col_idx])), rid);
            else if (auto* btree = std::get_if<BTreeIndex>(&index))
                btree->add(row[col_idx], rid);
            else
                std::unreachable();
        }
    }

    return rid;
}

SeqFile::Cursor Table::cursor() {
    return seq_file.cursor();
}

namespace catalog {

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

    [[nodiscard]] bool Catalog::create_table(Engine& eng,
                                             const std::string& name,
                                             std::vector<Column> columns,
                                             std::size_t pkey_col) const {
        assert(pkey_col < columns.size());

        auto path = table_path(name);
        if (FileManager::exists(path))
            return false;

        for (const auto& col : columns) {
            if (col.index) {
                switch (*col.index) {
                    case IndexType::HASH:
                        create_index<HashIndex>(eng, name, col.name);
                        break;
                    case IndexType::BTREE:
                        create_index<BTreeIndex>(eng, name, col.name);
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
                    case IndexType::HASH:
                        col_indices.emplace(i, HashIndex{eng, fid});
                        break;
                    case IndexType::BTREE:
                        col_indices.emplace(i, BTreeIndex{eng, fid});
                        break;
                    default:
                        std::unreachable();
                }
            }
        }

        return Table{seq_file, std::move(meta), std::move(col_nums), std::move(col_indices)};
    }

    bool Catalog::drop_table(const std::string& name) const {
        return FileManager::remove(table_path(name));
    }

}  // namespace catalog
