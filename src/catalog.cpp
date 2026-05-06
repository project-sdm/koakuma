#include "catalog.hpp"
#include <cstddef>
#include <filesystem>
#include <print>
#include <utility>
#include "engine/file_manager.hpp"
#include "index/btree.hpp"
#include "index/hash.hpp"
#include "seq_file.hpp"
#include "util.hpp"

using Table = catalog::Table;

Table::Table(SeqFile seq_file)
    : seq_file{seq_file},
      meta{seq_file.read_meta()} {
    const auto& cols = meta.columns;

    for (std::size_t i = 0; i < cols.size(); ++i)
        column_idx[cols[i].name] = i;
}

const SeqFile::Meta& Table::get_meta() const {
    return meta;
}

std::optional<Rid> Table::insert(const Row& row) {
    return seq_file.add(row);
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
        eng.file_mgr.init_file(fid);

        {
            SeqFile seq_file{eng, fid};
            seq_file.init(std::move(columns), pkey_col);
        }

        eng.file_mgr.close(fid);
        return true;
    }

    std::optional<Table> Catalog::get_table(Engine& eng, const std::string& name) const {
        FileId fid = TRY_OPT(eng.file_mgr.open(table_path(name)));
        SeqFile seq_file{eng, fid};
        return Table{seq_file};
    }

}  // namespace catalog
