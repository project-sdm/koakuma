#include "catalog.hpp"
#include <cstddef>
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
    return seq_file.insert(row);
}

SeqFile::Cursor Table::cursor() {
    return seq_file.cursor();
}

namespace catalog {

    std::filesystem::path table_path(const std::string& name) {
        return std::format("{}.table.bin", name);
    }

    [[nodiscard]] bool create_table(Engine& eng,
                                    const std::string& name,
                                    std::vector<Column> columns,
                                    std::size_t pkey_col) {
        assert(pkey_col < columns.size());

        auto path = table_path(name);
        if (FileManager::exists(path))
            return false;

        FileId fid = eng.file_mgr.open_create(path);

        {
            SeqFile seq_file{eng, fid};
            seq_file.init(std::move(columns), pkey_col);
        }

        eng.file_mgr.close(fid);
        return true;
    }

    std::optional<Table> get_table(Engine& eng, const std::string& name) {
        FileId fid = TRY_OPT(eng.file_mgr.open(table_path(name)));
        SeqFile seq_file{eng, fid};
        return Table{seq_file};
    }

}  // namespace catalog
