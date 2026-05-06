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

    class Catalog {
    private:
        std::filesystem::path data_path;

        [[nodiscard]] std::filesystem::path index_path(const std::string& table_name,
                                                       const std::string& col_name) const;

        template<typename Index>
        void create_index(Engine& eng,
                          const std::string& table_name,
                          const std::string& col_name) const;

    public:
        explicit Catalog(std::filesystem::path data_path);

        [[nodiscard]] std::filesystem::path table_path(const std::string& name) const;
        [[nodiscard]] std::optional<Table> get_table(Engine& eng, const std::string& name) const;

        [[nodiscard]] bool create_table(Engine& eng,
                                        const std::string& name,
                                        std::vector<Column> columns,
                                        std::size_t pkey_col) const;
    };

    template<typename Index>
    void Catalog ::create_index(Engine& eng,
                                const std::string& table_name,
                                const std::string& col_name) const {
        std::string path = index_path(table_name, col_name);

        FileId fid = eng.file_mgr.open_create(path);
        eng.file_mgr.init_file(fid);

        {
            Index index{eng, fid};
            index.init();
        }

        eng.file_mgr.close(fid);
    }

}  // namespace catalog

#endif
