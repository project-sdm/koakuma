#ifndef SEQ_FILE_HPP
#define SEQ_FILE_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "buffer_manager.hpp"
#include "file_manager.hpp"
#include "pack.hpp"
#include "types.hpp"

struct Rid {
    pnum_t pnum;
    u32 slot_idx;

    Rid() = delete;

    Rid(pnum_t pnum, u32 slot_idx);
};

using Value = std::variant<int, bool, std::string>;
using Row = std::vector<Value>;

// TODO: implement pack for Row

enum class ColumnType : u8 {
    INT,
    BOOL,
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

static constexpr pnum_t PAGE_NIL = -1;

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
    struct PageHeader {
        u32 slot_cnt = 0;
        u32 data_begin = PAGE_SIZE;
    };

    struct Slot {
        u32 pos;
        u32 len;
        bool active;

        Slot() = default;
        Slot(u32 pos, u32 len, bool active);
    };

    FileManager& file_mgr;
    BufferManager& buf_mgr;

    FileId fid;

    [[nodiscard]] static u32 slot_offset(u32 slot_idx);

    [[nodiscard]] static Slot read_slot(const BufferManager::PageGuard& page, u32 slot_idx);
    static void write_slot(const BufferManager::PageGuard& page, u32 slot_idx, const Slot& slot);
    [[nodiscard]] static Row read_row(const BufferManager::PageGuard& page, u32 slot_idx);

    [[nodiscard]] std::optional<pnum_t> find_page_by_pkey(const Value& pkey);
    [[nodiscard]] std::optional<Rid> find_by_pkey_in_main_pages(const Value& pkey);
    [[nodiscard]] std::optional<u32> find_by_pkey_in_main_page(pnum_t pnum, const Value& pkey);
    [[nodiscard]] std::optional<Rid> find_by_pkey_in_aux_page(const Value& pkey);
    [[nodiscard]] std::optional<Rid> find_rid_by_pkey(const Value& pkey);

    Rid insert_into_aux(const Row& row);

    static pnum_t aux_pnum(const SeqHeader& file_hdr);

public:
    explicit SeqFile(FileManager& file_mgr, BufferManager& buf_mgr, FileId fid);

    SeqFile(const SeqFile&) = delete;
    SeqFile(SeqFile&&) = delete;

    SeqFile& operator=(const SeqFile&) = delete;
    SeqFile& operator=(SeqFile&&) = delete;

    void init(std::vector<Column> columns, u32 pkey_col);

    [[nodiscard]] Row read_row(Rid rid);

    [[nodiscard]] std::optional<Row> find_by_pkey(const Value& pkey);

    std::optional<Rid> insert(const Row& row);
};

#endif
