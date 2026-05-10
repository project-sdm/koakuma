#ifndef FILE_COMMON_HPP
#define FILE_COMMON_HPP

#include <array>
#include <string>
#include <variant>
#include "engine/file_manager.hpp"
#include "types.hpp"

static constexpr pnum_t PAGE_NIL = -1;

struct Rid {
    pnum_t pnum = PAGE_NIL;
    u32 slot_idx = 0;

    Rid();
    Rid(pnum_t pnum, u32 slot_idx);

    bool operator==(const Rid& other) const;
    bool operator<(const Rid& other) const;
};

enum class ColumnType : u8 {
    Int,
    Bool,
    Real,
    VarChar,
    Point2d,
};

enum class IndexType : u8 {
    Hash,
    BTree,
    RTree,
};

enum class FileType : u8 {
    Seq,
    Append,
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

template<std::size_t N>
using Point = std::array<f64, N>;

using Value = std::variant<int, bool, f64, std::string, Point<2>>;
using Row = std::vector<Value>;

class UnknownFile {
public:
    struct Header {
        FileType file_type{};
        std::vector<Column> columns;

        Header();
        Header(FileType file_type, std::vector<Column> columns);
    };
};

template<>
struct pack::PackSize<UnknownFile::Header> {
    std::size_t operator()(const UnknownFile::Header& hdr) const {
        return pack_size(hdr.file_type) + pack_size(hdr.columns);
    }
};

template<>
struct pack::Pack<UnknownFile::Header> {
    void operator()(const UnknownFile::Header& hdr, u8*& dest) const {
        pack<>(hdr.file_type, dest);
        pack<>(hdr.columns, dest);
    }
};

template<>
struct pack::Unpack<UnknownFile::Header> {
    void operator()(UnknownFile::Header& dest, const u8*& src) const {
        unpack<>(dest.file_type, src);
        unpack<>(dest.columns, src);
    }
};

// class File {
// private:
//     std::variant<SeqFile> file;
//
// public:
//
//     [[nodiscard]] Row read_rid(Rid rid) const;
//     // std::optional<std::pair<Rid, InsertResult>> add(const Row& row);
//     [[nodiscard]] std::optional<Row> search(const Value& pkey);
//     // [[nodiscard]] RangeCursor range_search(const Value& pkey_low, const Value& pkey_high);
//     std::optional<Rid> remove(const Value& pkey);
//
// };

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
