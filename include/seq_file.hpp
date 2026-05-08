#ifndef SEQ_FILE_HPP
#define SEQ_FILE_HPP

#include <cstddef>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "engine/engine.hpp"
#include "engine/file_manager.hpp"
#include "layout/slotted_page.hpp"
#include "pack.hpp"
#include "parser/ast.hpp"
#include "types.hpp"
#include "util.hpp"

static constexpr pnum_t PAGE_NIL = -1;

struct Rid {
    pnum_t pnum = PAGE_NIL;
    u32 slot_idx = 0;

    Rid();
    Rid(pnum_t pnum, u32 slot_idx);

    bool operator==(const Rid& other) const;
    bool operator<(const Rid& other) const;
};

template<>
struct pack::PackSize<Rid> {
    std::size_t operator()(const Rid& rid) const {
        return pack_size<>(rid.pnum) + pack_size<>(rid.slot_idx);
    }
};

template<>
struct pack::Pack<Rid> {
    void operator()(const Rid& rid, u8*& dest) const {
        pack<>(rid.pnum, dest);
        pack<>(rid.slot_idx, dest);
    }
};

template<>
struct pack::Unpack<Rid> {
    void operator()(Rid& dest, const u8*& src) const {
        unpack<>(dest.pnum, src);
        unpack<>(dest.slot_idx, src);
    }
};

using Value = std::variant<int, bool, f64, std::string, parser::Point2D>;
using Row = std::vector<Value>;

enum class ColumnType : u8 {
    INT,
    BOOL,
    REAL,
    VARCHAR,
    POINT2D,
};

enum class IndexType : u8 {
    HASH,
    BTREE,
};

struct Column {
    std::string name;
    ColumnType type{};
    std::optional<IndexType> index;

    Column();
    Column(std::string name, ColumnType type, std::optional<IndexType> index);
};

template<>
struct pack::PackSize<Column> {
    std::size_t operator()(const Column& col) const {
        return pack_size<>(col.name) + pack_size<>(col.type) + pack_size<>(col.index);
    }
};

template<>
struct pack::Pack<Column> {
    void operator()(const Column& col, u8*& dest) const {
        pack<>(col.name, dest);
        pack<>(col.type, dest);
        pack<>(col.index, dest);
    }
};

template<>
struct pack::Unpack<Column> {
    void operator()(Column& dest, const u8*& src) const {
        unpack<>(dest.name, src);
        unpack<>(dest.type, src);
        unpack<>(dest.index, src);
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
public:
    enum class InsertResult : u8;

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

    std::pair<Rid, InsertResult> insert_into_aux(const Row& row);

    static pnum_t calc_aux_pnum(const SeqHeader& file_hdr);

    void rebuild(SeqHeader& file_hdr);

public:
    enum class InsertResult : u8 {
        NONE,
        REBUILD,
    };

    class Cursor {
    private:
        SeqFile& seq_file;
        SeqHeader seq_hdr;
        std::optional<SeqPage> page_buf = std::nullopt;
        u32 cur_slot = 0;
        pnum_t cur_pnum = 1;

        SeqPage& page();

    public:
        using value_type = std::pair<Rid, Row>;

        explicit Cursor(SeqFile& seq_file);

        std::optional<value_type> next();
    };

    class RangeCursor {
    private:
        SeqFile& seq_file;
        SeqHeader seq_hdr;
        std::optional<SeqPage> page_buf = std::nullopt;
        u32 cur_slot = 0;
        pnum_t cur_pnum = 1;

        Value pkey_low;
        Value pkey_high;

        SeqPage& page();

    public:
        using value_type = std::pair<Rid, Row>;

        explicit RangeCursor(SeqFile& seq_file, Value pkey_low, Value pkey_high);

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

    [[nodiscard]] Row read_rid(Rid rid) const;
    std::optional<std::pair<Rid, InsertResult>> add(const Row& row);
    [[nodiscard]] std::optional<Row> search(const Value& pkey);
    [[nodiscard]] RangeCursor range_search(const Value& pkey_low, const Value& pkey_high);
    std::optional<Rid> remove(const Value& pkey);

    Cursor cursor();
};

template<>
struct std::formatter<Value, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const Value& val, std::format_context& ctx) {
        if (const auto* s = std::get_if<std::string>(&val))
            return std::format_to(ctx.out(), "'{}'", *s);

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

template<>
struct std::formatter<Rid, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const Rid& rid, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Rid({}, {})", rid.pnum, rid.slot_idx);
    }
};

#endif
