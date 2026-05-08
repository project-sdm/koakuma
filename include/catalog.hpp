#ifndef CATALOG_HPP
#define CATALOG_HPP

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <variant>
#include "engine/engine.hpp"
#include "index/btree.hpp"
#include "index/hash.hpp"
#include "seq_file.hpp"

namespace catalog {
    using AnyIndex = std::variant<BTreeIndex, HashIndex>;

    struct DuplicatePrimaryKey {
        Value pkey;

        explicit DuplicatePrimaryKey(Value pkey);
    };

    using InsertionError = std::variant<DuplicatePrimaryKey, ValueNotHashable>;

    class Table {
    public:
        explicit Table(SeqFile seq_file,
                       SeqFile::Meta meta,
                       std::unordered_map<std::string, std::size_t> col_nums,
                       std::unordered_map<std::size_t, AnyIndex> col_indices);

        const SeqFile::Meta& get_meta() const;
        [[nodiscard]] std::size_t col_num(const std::string& col_name) const;
        [[nodiscard]] std::size_t pkey_col_num() const;

        std::expected<Rid, InsertionError> insert(const Row& row);

        SeqFile::Cursor cursor();
        AnyIndex* get_index(const std::string& col_name);

        SeqFile& get_seq_file();

    private:
        SeqFile seq_file;
        SeqFile::Meta meta;
        std::unordered_map<std::string, std::size_t> col_nums;
        std::unordered_map<std::size_t, AnyIndex> col_indices;
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

        [[nodiscard]] bool drop_table(const std::string& name) const;
    };

    template<typename Index>
    void Catalog ::create_index(Engine& eng,
                                const std::string& table_name,
                                const std::string& col_name) const {
        std::string path = index_path(table_name, col_name);

        FileId fid = eng.file_mgr.open_create(path);

        {
            Index index{eng, fid};
            index.init();
        }

        eng.file_mgr.close(fid);
    }

}  // namespace catalog

template<>
struct std::formatter<catalog::DuplicatePrimaryKey, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const catalog::DuplicatePrimaryKey& err, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Duplicate primary key: {}.", err.pkey);
    }
};

template<>
struct std::formatter<catalog::InsertionError, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const catalog::InsertionError& err, std::format_context& ctx) {
        return std::visit([&](auto&& v) { return std::format_to(ctx.out(), "{}", v); }, err);
    }
};

#endif
