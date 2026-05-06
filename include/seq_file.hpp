#ifndef SEQ_FILE_HPP
#define SEQ_FILE_HPP

#include <cstddef>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "engine/engine.hpp"
#include "engine/file_manager.hpp"
#include "layout/slotted_page.hpp"
#include "pack.hpp"
#include "types.hpp"
#include "util.hpp"

struct Rid {
    pnum_t pnum;
    u32 slot_idx;

    Rid() = delete;
    Rid(pnum_t pnum, u32 slot_idx);

    bool operator==(const Rid& other) const;
    bool operator<(const Rid& other) const;
};

static constexpr pnum_t PAGE_NIL = -1;

using Value = std::variant<int, bool, f64, std::string>;
using HashValue = std::variant<int, bool, std::string>;
using Row = std::vector<Value>;

enum class ColumnType : u8 {
    INT,
    BOOL,
    FLOAT,
    STRING,
};

struct Column {
    std::string name;
    ColumnType type{};
    bool is_unique = false;

    Column();
    Column(std::string name, ColumnType type);
};

template<>
struct pack::PackSize<Column> {
    std::size_t operator()(const Column& col) const {
        return pack_size<>(col.name) + pack_size<>(col.type) + pack_size<>(col.is_unique);
    }
};

template<>
struct pack::Pack<Column> {
    void operator()(const Column& col, u8*& dest) const {
        pack<>(col.name, dest);
        pack<>(col.type, dest);
        pack<>(col.is_unique, dest);
    }
};

template<>
struct pack::Unpack<Column> {
    void operator()(Column& dest, const u8*& src) const {
        unpack<>(dest.name, src);
        unpack<>(dest.type, src);
        unpack<>(dest.is_unique, src);
    }
};

struct SeqHeader {
    std::vector<Column> columns;
    u32 pkey_col = -1;
    pnum_t main_pages = 0;

    SeqHeader() = default;
    SeqHeader(std::vector<Column> columns, u32 pkey_col);
};

template<>
struct pack::PackSize<SeqHeader> {
    std::size_t operator()(const SeqHeader& hdr) const {
        return pack_size<>(hdr.columns) + pack_size<>(hdr.pkey_col) + pack_size<>(hdr.main_pages);
    }
};

template<>
struct pack::Pack<SeqHeader> {
    void operator()(const SeqHeader& hdr, u8*& dest) const {
        pack<>(hdr.columns, dest);
        pack<>(hdr.pkey_col, dest);
        pack<>(hdr.main_pages, dest);
    }
};

template<>
struct pack::Unpack<SeqHeader> {
    void operator()(SeqHeader& dest, const u8*& src) const {
        unpack<>(dest.columns, src);
        unpack<>(dest.pkey_col, src);
        unpack<>(dest.main_pages, src);
    }
};

class SeqFile {
private:
    struct PageHeader {};

    struct SlotExtra {
        bool active;

        explicit SlotExtra(bool active);
    };

    using SeqPage = SlottedPage<PageHeader, SlotExtra, Row>;

    Engine& eng;
    FileId fid;

    [[nodiscard]] std::optional<pnum_t> find_main_page_by_pkey(const Value& pkey);
    [[nodiscard]] std::optional<Rid> find_by_pkey_in_main_pages(const Value& pkey);
    [[nodiscard]] std::optional<u32> find_slot_by_pkey_in_main_page(pnum_t pnum, const Value& pkey);
    [[nodiscard]] std::optional<Rid> find_by_pkey_in_aux_page(const Value& pkey);
    [[nodiscard]] std::optional<Rid> find_rid_by_pkey_in_all_pages(const Value& pkey);

    Rid insert_into_aux(const Row& row);

    static pnum_t calc_aux_pnum(const SeqHeader& file_hdr);

    void rebuild(SeqHeader& file_hdr);

public:
    class Cursor {
        SeqFile& seq_file;
        SeqHeader seq_hdr;
        std::optional<SeqPage> page_buf = std::nullopt;
        u32 cur_slot = 0;
        pnum_t cur_pnum = 1;

        SeqPage& page();

    public:
        using value_type = Row;

        explicit Cursor(SeqFile& seq_file);

        std::optional<value_type> next();
    };

    static_assert(util::iter<Cursor>);

    SeqFile(Engine& engine, FileId fid);

    void init(std::vector<Column> columns, u32 pkey_col);

    struct Meta {
        std::vector<Column> columns;
        std::size_t pkey_col;

        Meta(std::vector<Column> columns, std::size_t pkey_col);
    };

    [[nodiscard]] Meta read_meta();

    std::optional<Rid> add(const Row& row);
    [[nodiscard]] std::optional<Row> search(const Value& pkey);
    std::optional<Rid> remove(const Value& pkey);

    Cursor cursor();
};

template<>
struct std::formatter<Value, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const Value& val, std::format_context& ctx) {
        return std::visit([&](auto&& v) { return std::format_to(ctx.out(), "{}", v); }, val);
    }
};

template<>
struct std::formatter<HashValue, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const HashValue& val, std::format_context& ctx) {
        return std::visit([&](auto&& v) { return std::format_to(ctx.out(), "{}", v); }, val);
    }
};

template<>
struct std::formatter<Row, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const Row& row, std::format_context& ctx) {
        auto out = ctx.out();
        std::format_to(out, "(");

        if (!row.empty()) {
            std::format_to(out, "{}", row[0]);

            for (std::size_t i = 1; i < row.size(); ++i)
                std::format_to(out, ", {}", row[i]);
        }

        std::format_to(out, ")");
        return out;
    }
};

#endif
