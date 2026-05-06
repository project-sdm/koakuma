#ifndef CATALOG_HPP
#define CATALOG_HPP

#include <filesystem>
#include <string>
#include "engine/engine.hpp"
#include "seq_file.hpp"

namespace catalog {
    class Table {
    private:
        SeqFile seq_file;
        SeqFile::Meta meta;
        std::unordered_map<std::string, std::size_t> column_idx;

    public:
        explicit Table(SeqFile seq_file);

        const SeqFile::Meta& get_meta() const;

        std::optional<Rid> insert(const Row& row);

        // coupled to seqfile for now (hopefully not for long)
        SeqFile::Cursor cursor();
    };

    std::filesystem::path table_path(const std::string& name);

    [[nodiscard]] bool create_table(Engine& eng,
                                    const std::string& name,
                                    std::vector<Column> columns,
                                    std::size_t pkey_col);

    std::optional<Table> get_table(Engine& eng, const std::string& name);

}  // namespace catalog

#endif
