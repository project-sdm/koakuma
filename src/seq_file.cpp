#include "seq_file.hpp"
#include <cassert>
#include <optional>
#include <utility>
#include "engine/buffer_manager.hpp"
#include "engine/engine.hpp"
#include "engine/file_manager.hpp"
#include "util.hpp"

Rid::Rid(pnum_t pnum, u32 slot_idx)
    : pnum{pnum},
      slot_idx{slot_idx} {}

bool Rid::operator==(const Rid& other) const = default;

Column::Column() = default;

Column::Column(std::string name, ColumnType type)
    : name{std::move(name)},
      type{type} {}

SeqHeader::SeqHeader(std::vector<Column> columns, u32 pkey_col)
    : columns{std::move(columns)},
      pkey_col{pkey_col} {}

SeqFile::SeqFile(Engine& engine, FileId fid)
    : eng{engine},
      fid{fid} {}

void SeqFile::init(std::vector<Column> columns, u32 pkey_col) {
    SeqHeader file_hdr{std::move(columns), pkey_col};
    eng.file_mgr.write_user_header(fid, file_hdr);

    SeqPage aux_page{eng.buf_mgr.fetch_page(fid, aux_pnum(file_hdr))};
    aux_page.init();
}

std::optional<u32> SeqFile::find_slot_by_pkey_in_main_page(pnum_t pnum, const Value& pkey) {
    auto file_hdr = eng.file_mgr.read_user_header<SeqHeader>(fid);

    SeqPage page{eng.buf_mgr.fetch_page(fid, pnum)};

    u32 low = 0;
    u32 high = page.slot_cnt();

    while (low < high) {
        u32 mid = (low + high) / 2;

        Row row = page.read_data(mid);

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
    auto file_hdr = eng.file_mgr.read_user_header<SeqHeader>(fid);
    assert(file_hdr.pkey_col < file_hdr.columns.size());

    pnum_t plow = 1;
    pnum_t phigh = plow + file_hdr.main_pages;

    while (plow < phigh) {
        pnum_t pmid = (plow + phigh) / 2;
        assert(pmid > 0);

        SeqPage page{eng.buf_mgr.fetch_page(fid, pmid)};

        auto fst_row = page.read_data(0);
        if (pkey < fst_row[file_hdr.pkey_col]) {
            // continue to the left
            phigh = pmid;
            continue;
        }

        auto last_row = page.read_data(page.slot_cnt() - 1);

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
    auto file_hdr = eng.file_mgr.read_user_header<SeqHeader>(fid);
    SeqPage page{eng.buf_mgr.fetch_page(fid, aux_pnum(file_hdr))};

    for (u32 i = 0; i < page.slot_cnt(); ++i) {
        auto row = page.read_data(i);

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

std::optional<Row> SeqFile::search(const Value& pkey) {
    auto rid = TRY_OPT(find_rid_by_pkey_in_all_pages(pkey));

    SeqPage page{eng.buf_mgr.fetch_page(fid, rid.pnum)};
    auto extra = page.read_slot_extra(rid.slot_idx);

    if (!extra.active)
        return std::nullopt;

    return page.read_data(rid.slot_idx);
}

std::optional<Rid> SeqFile::add(const Row& row) {
    auto file_hdr = eng.file_mgr.read_user_header<SeqHeader>(fid);
    const Value& pkey = row[file_hdr.pkey_col];

    if (auto rid = find_rid_by_pkey_in_all_pages(pkey)) {
        SeqPage page{eng.buf_mgr.fetch_page(fid, rid->pnum)};
        auto extra = page.read_slot_extra(rid->slot_idx);

        if (extra.active)
            return std::nullopt;
    }

    return insert_into_aux(row);
}

// assumes that `row`'s primary key is not already in the aux page
Rid SeqFile::insert_into_aux(const Row& row) {
    auto file_hdr = eng.file_mgr.read_user_header<SeqHeader>(fid);
    pnum_t pnum = aux_pnum(file_hdr);

    SeqPage aux_page{eng.buf_mgr.fetch_page(fid, pnum)};

    u32 new_slot_idx = aux_page.slot_cnt();

    if (aux_page.will_fit(row)) {
        aux_page.push_back(SlotExtra{true}, row);
        return Rid{pnum, new_slot_idx};
    }

    FileId tmp_fid = eng.file_mgr.open_create("tmp.bin");

    eng.file_mgr.close(tmp_fid);

    assert(false && "TODO: implement rebuild");
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

        if (!page)
            page.emplace(seq_file.eng.buf_mgr.fetch_page(seq_file.fid, cur_pnum));

        SlotExtra extra = page->read_slot_extra(cur_slot);

        std::optional<Row> row = std::nullopt;
        if (extra.active)
            row = page->read_data(cur_slot);

        cur_slot += 1;
        assert(cur_slot <= page->slot_cnt());

        if (cur_slot == page->slot_cnt()) {
            page.reset();
            cur_slot = 0;

            auto file_hdr = seq_file.eng.file_mgr.read_user_header<SeqHeader>(seq_file.fid);

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

std::optional<Rid> SeqFile::remove(const Value& pkey) {
    auto rid = TRY_OPT(find_rid_by_pkey_in_all_pages(pkey));

    SeqPage page{eng.buf_mgr.fetch_page(fid, rid.pnum)};
    auto extra = page.read_slot_extra(rid.slot_idx);

    if (!extra.active)
        return std::nullopt;

    extra.active = false;
    page.write_slot_extra(rid.slot_idx, extra);

    return rid;
}
SeqFile::Meta::Meta(std::vector<Column> columns, std::size_t pkey_col)
    : columns{std::move(columns)},
      pkey_col{pkey_col} {}

SeqFile::Meta SeqFile::read_meta() {
    auto file_hdr = eng.file_mgr.read_user_header<SeqHeader>(fid);
    return Meta{file_hdr.columns, file_hdr.pkey_col};
}
