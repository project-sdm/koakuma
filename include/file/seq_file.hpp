#ifndef SEQ_FILE_HPP
#define SEQ_FILE_HPP

#include <cstddef>
#include <cstdio>
#include <optional>
#include <vector>
#include "common.hpp"
#include "engine/engine.hpp"
#include "engine/file_manager.hpp"
#include "layout/slotted_page.hpp"
#include "pack.hpp"
#include "types.hpp"
#include "util.hpp"

struct SeqHeader {
    UnknownFile::Header hdr;
    std::size_t pkey_col = 0;
    pnum_t main_pages = 0;

    SeqHeader();
    SeqHeader(std::vector<Column> columns, u32 pkey_col);
};

template<>
struct pack::PackSize<SeqHeader> {
    std::size_t operator()(const SeqHeader& hdr) const {
        return pack_size<>(hdr.hdr) + pack_size<>(hdr.main_pages) + pack_size<>(hdr.pkey_col);
    }
};

template<>
struct pack::Pack<SeqHeader> {
    void operator()(const SeqHeader& hdr, u8*& dest) const {
        pack<>(hdr.hdr, dest);
        pack<>(hdr.main_pages, dest);
        pack<>(hdr.pkey_col, dest);
    }
};

template<>
struct pack::Unpack<SeqHeader> {
    void operator()(SeqHeader& dest, const u8*& src) const {
        unpack<>(dest.hdr, src);
        unpack<>(dest.main_pages, src);
        unpack<>(dest.pkey_col, src);
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
        None,
        Rebuild,
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

    [[nodiscard]] u32 pkey_col_num() const;

    [[nodiscard]] Row read_rid(Rid rid) const;
    std::optional<std::pair<Rid, InsertResult>> add(const Row& row);
    [[nodiscard]] std::optional<Row> search(const Value& pkey);
    [[nodiscard]] RangeCursor range_search(const Value& pkey_low, const Value& pkey_high);
    std::optional<std::pair<Rid, Row>> remove(const Value& pkey);

    Cursor cursor();
};

#endif
