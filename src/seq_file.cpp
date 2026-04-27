#include "seq_file.hpp"
#include <cassert>
#include <optional>
#include <print>
#include <utility>
#include "buffer_manager.hpp"
#include "file_manager.hpp"
#include "pack.hpp"
#include "util.hpp"

Rid::Rid(pnum_t pnum, u32 slot_idx)
    : pnum{pnum},
      slot_idx{slot_idx} {}

bool Rid::operator==(const Rid& other) const = default;

Column::Column() = default;

Column::Column(std::string name, ColumnType type)
    : name{std::move(name)},
      type{type} {}

SeqFile::Slot::Slot(u32 pos, u32 len, bool active)
    : pos{pos},
      len{len},
      active{active} {}

SeqHeader::SeqHeader(std::vector<Column> columns, u32 pkey_col)
    : columns{std::move(columns)},
      pkey_col{pkey_col} {}

SeqFile::SeqFile(FileManager& file_mgr, BufferManager& buf_mgr, FileId fid)
    : file_mgr{file_mgr},
      buf_mgr{buf_mgr},
      fid{fid} {}

void SeqFile::init(std::vector<Column> columns, u32 pkey_col) {
    SeqHeader file_hdr{std::move(columns), pkey_col};
    file_mgr.write_user_header(fid, file_hdr);

    auto aux_page = buf_mgr.fetch_page(fid, aux_pnum(file_hdr));
    util::span_write(aux_page.data(), 0, PageHeader{});
}

u32 SeqFile::slot_offset(u32 slot_idx) {
    return sizeof(PageHeader) + (sizeof(Slot) * slot_idx);
}

SeqFile::Slot SeqFile::read_slot(const BufferManager::PageGuard& page, u32 slot_idx) {
    return util::span_read<Slot>(page.const_data(), slot_offset(slot_idx));
}

void SeqFile::write_slot(const BufferManager::PageGuard& page, u32 slot_idx, const Slot& slot) {
    util::span_write<Slot>(page.data(), slot_offset(slot_idx), slot);
}

Row SeqFile::read_row(const BufferManager::PageGuard& page, u32 slot_idx) {
    auto slot = util::span_read<Slot>(page.const_data(), slot_offset(slot_idx));
    return read_row(page, slot);
}

Row SeqFile::read_row(const BufferManager::PageGuard& page, const Slot& slot) {
    return pack::unpack_alloc<Row>(page.const_data().subspan(slot.pos).data());
}

Row SeqFile::read_row(Rid rid) {
    auto page = buf_mgr.fetch_page(fid, rid.pnum);
    return read_row(page, rid.slot_idx);
}

std::optional<u32> SeqFile::find_slot_by_pkey_in_main_page(pnum_t pnum, const Value& pkey) {
    auto file_hdr = file_mgr.read_user_header<SeqHeader>(fid);

    auto page = buf_mgr.fetch_page(fid, pnum);
    auto page_hdr = util::span_read<PageHeader>(page.const_data(), 0);

    u32 low = 0;
    u32 high = page_hdr.slot_cnt;

    while (low < high) {
        u32 mid = (low + high) / 2;

        Row row = read_row(page, mid);

        if (pkey == row[file_hdr.pkey_col])
            return mid;

        if (pkey < row[file_hdr.pkey_col])
            high = mid - 1;
        else
            low = mid + 1;
    }

    return std::nullopt;
}

std::optional<pnum_t> SeqFile::find_main_page_by_pkey(const Value& pkey) {
    auto file_hdr = file_mgr.read_user_header<SeqHeader>(fid);
    assert(file_hdr.pkey_col < file_hdr.columns.size());

    pnum_t plow = 1;
    pnum_t phigh = plow + file_hdr.main_pages;

    while (plow < phigh) {
        pnum_t pmid = (plow + phigh) / 2;
        assert(pmid > 0);

        auto page = buf_mgr.fetch_page(fid, pmid);

        auto fst_row = read_row(page, 0);
        if (pkey < fst_row[file_hdr.pkey_col]) {
            // continue to the left
            phigh = pmid;
            continue;
        }

        auto page_hdr = util::span_read<PageHeader>(page.const_data(), 0);
        auto last_row = read_row(page, page_hdr.slot_cnt - 1);

        if (pkey > last_row[file_hdr.pkey_col]) {
            // continue to the right
            plow = pmid + 1;
            continue;
        }

        // this is the correct page
        return pmid;
    }

    return std::nullopt;
}
std::optional<Rid> SeqFile::find_by_pkey_in_main_pages(const Value& pkey) {
    auto pnum = TRY_OPT(find_main_page_by_pkey(pkey));
    u32 slot_idx = TRY_OPT(find_slot_by_pkey_in_main_page(pnum, pkey));
    return Rid{pnum, slot_idx};
}

pnum_t SeqFile::aux_pnum(const SeqHeader& file_hdr) {
    return 1 + file_hdr.main_pages;
}

std::optional<Rid> SeqFile::find_by_pkey_in_aux_page(const Value& pkey) {
    auto file_hdr = file_mgr.read_user_header<SeqHeader>(fid);
    auto page = buf_mgr.fetch_page(fid, aux_pnum(file_hdr));
    auto page_hdr = util::span_read<PageHeader>(page.const_data(), 0);

    for (u32 i = 0; i < page_hdr.slot_cnt; ++i) {
        auto row = read_row(page, i);

        if (pkey == row[file_hdr.pkey_col])
            return Rid{aux_pnum(file_hdr), i};
    }

    return std::nullopt;
}

std::optional<Rid> SeqFile::find_rid_by_pkey_in_all_pages(const Value& pkey) {
    if (auto rid = find_by_pkey_in_main_pages(pkey))
        return rid;

    return find_by_pkey_in_aux_page(pkey);
}

std::optional<Row> SeqFile::find_by_pkey(const Value& pkey) {
    auto rid = TRY_OPT(find_rid_by_pkey_in_all_pages(pkey));

    auto page = buf_mgr.fetch_page(fid, rid.pnum);
    auto slot = util::span_read<Slot>(page.const_data(), slot_offset(rid.slot_idx));

    if (!slot.active)
        return std::nullopt;

    return read_row(page, slot);
}

std::optional<Rid> SeqFile::insert(const Row& row) {
    auto file_hdr = file_mgr.read_user_header<SeqHeader>(fid);
    const Value& pkey = row[file_hdr.pkey_col];

    if (auto rid = find_rid_by_pkey_in_all_pages(pkey)) {
        auto page = buf_mgr.fetch_page(fid, rid->pnum);
        auto slot = read_slot(page, rid->slot_idx);

        if (slot.active)
            return std::nullopt;
    }

    return insert_into_aux(row);
}

// assumes that `row`'s primary key is not already in the aux page
Rid SeqFile::insert_into_aux(const Row& row) {
    auto file_hdr = file_mgr.read_user_header<SeqHeader>(fid);
    pnum_t pnum = aux_pnum(file_hdr);

    auto aux_page = buf_mgr.fetch_page(fid, pnum);
    auto aux_hdr = util::span_read<PageHeader>(aux_page.const_data(), 0);

    u32 new_slot_idx = aux_hdr.slot_cnt;

    u32 row_size = pack::pack_size(row);
    u32 new_data_begin = aux_hdr.data_begin - row_size;
    std::println("new data begin: {}", new_data_begin);

    // TODO: replace this assertion with rebuild()
    assert(slot_offset(new_slot_idx) + sizeof(Slot) <= new_data_begin);

    u8* row_dest = aux_page.data().subspan(new_data_begin).data();
    pack::pack(row, row_dest);

    Slot new_slot{new_data_begin, row_size, true};
    write_slot(aux_page, new_slot_idx, new_slot);

    aux_hdr.data_begin = new_data_begin;
    aux_hdr.slot_cnt += 1;
    util::span_write(aux_page.data(), 0, aux_hdr);

    return Rid{pnum, new_slot_idx};
}

using Cursor = SeqFile::Cursor;

Cursor::Cursor(SeqFile& seq_file)
    : seq_file{seq_file} {}

Cursor SeqFile::cursor() {
    return Cursor{*this};
}

std::optional<Row> Cursor::next() {
    while (true) {
        if (cur_pnum == PAGE_NIL)
            return std::nullopt;

        if (!cur_page)
            cur_page.emplace(seq_file.buf_mgr.fetch_page(seq_file.fid, cur_pnum));

        Slot slot = read_slot(*cur_page, cur_slot);

        std::optional<Row> row = std::nullopt;
        if (slot.active)
            row = read_row(*cur_page, slot);

        // PERF: this could cause lots of reads, but saving it as a field of
        // `Cursor` might cause concurrency trouble.
        auto page_hdr = util::span_read<PageHeader>(cur_page->const_data(), 0);
        cur_slot += 1;
        assert(cur_slot <= page_hdr.slot_cnt);

        if (cur_slot == page_hdr.slot_cnt) {
            cur_page.reset();
            cur_slot = 0;

            auto file_hdr = seq_file.file_mgr.read_user_header<SeqHeader>(seq_file.fid);

            assert(cur_pnum <= aux_pnum(file_hdr));

            if (cur_pnum == aux_pnum(file_hdr))
                cur_pnum = PAGE_NIL;
            else
                cur_pnum += 1;
        }

        if (row)
            return row;
    }
}

std::optional<Rid> SeqFile::delete_by_pkey(const Value& pkey) {
    auto rid = TRY_OPT(find_rid_by_pkey_in_all_pages(pkey));
    auto page = buf_mgr.fetch_page(fid, rid.pnum);
    auto slot = read_slot(page, rid.slot_idx);

    if (!slot.active)
        return std::nullopt;

    slot.active = false;
    write_slot(page, rid.slot_idx, slot);

    return rid;
}
