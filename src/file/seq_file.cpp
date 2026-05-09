#include "file/seq_file.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <print>
#include <stdexcept>
#include <utility>
#include "engine/buffer_manager.hpp"
#include "engine/engine.hpp"
#include "engine/file_manager.hpp"
#include "file/common.hpp"
#include "util.hpp"

Rid::Rid() = default;

Rid::Rid(pnum_t pnum, u32 slot_idx)
    : pnum{pnum},
      slot_idx{slot_idx} {}

bool Rid::operator==(const Rid& other) const = default;

Column::Column() = default;

Column::Column(std::string name, ColumnType type, std::optional<IndexType> index)
    : name{std::move(name)},
      type{type},
      index{index} {}

SeqFile::SlotExtra::SlotExtra(bool active)
    : active{active} {}

SeqHeader::SeqHeader(std::vector<Column> columns, u32 pkey_col)
    : UnknownFile::Header{FileType::Seq, std::move(columns), pkey_col} {}

SeqFile::SeqFile(Engine& engine, FileId fid)
    : eng{engine},
      fid{fid} {}

void SeqFile::init(std::vector<Column> columns, u32 pkey_col) {
    eng.file_mgr.init_file(fid);

    SeqHeader file_hdr{std::move(columns), pkey_col};
    eng.file_mgr.write_user_header(fid, file_hdr);

    SeqPage aux_page{eng.buf_mgr.fetch_page(fid, calc_aux_pnum(file_hdr))};
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

pnum_t SeqFile::calc_aux_pnum(const SeqHeader& file_hdr) {
    return 1 + file_hdr.main_pages;
}

std::optional<Rid> SeqFile::find_by_pkey_in_aux_page(const Value& pkey) {
    auto file_hdr = eng.file_mgr.read_user_header<SeqHeader>(fid);
    SeqPage page{eng.buf_mgr.fetch_page(fid, calc_aux_pnum(file_hdr))};

    for (u32 i = 0; i < page.slot_cnt(); ++i) {
        auto row = page.read_data(i);

        if (pkey == row[file_hdr.pkey_col])
            return Rid{calc_aux_pnum(file_hdr), i};
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
    return read_rid(rid);
}

Row SeqFile::read_rid(Rid rid) const {
    SeqPage page{eng.buf_mgr.fetch_page(fid, rid.pnum)};
    auto extra = page.read_slot_extra(rid.slot_idx);

    if (!extra.active)
        throw std::invalid_argument("slot is inactive");

    return page.read_data(rid.slot_idx);
}

std::optional<std::pair<Rid, SeqFile::InsertResult>> SeqFile::add(const Row& row) {
    auto file_hdr = eng.file_mgr.read_user_header<SeqHeader>(fid);
    const Value& pkey = row[file_hdr.pkey_col];

    if (auto rid = find_rid_by_pkey_in_all_pages(pkey)) {
        SeqPage page{eng.buf_mgr.fetch_page(fid, rid->pnum)};

        if (page.read_slot_extra(rid->slot_idx).active)
            return std::nullopt;
    }

    return insert_into_aux(row);
}

void SeqFile::rebuild(SeqHeader& file_hdr) {
    std::vector<Row> aux_rows;

    {
        pnum_t aux_pnum = calc_aux_pnum(file_hdr);
        SeqPage aux_page{eng.buf_mgr.fetch_page(fid, aux_pnum)};

        aux_rows.reserve(aux_page.slot_cnt());

        for (u32 i = 0; i < aux_page.slot_cnt(); ++i) {
            if (aux_page.read_slot_extra(i).active)
                aux_rows.push_back(aux_page.read_data(i));
        }
    }

    u32 pkey_col = file_hdr.pkey_col;
    std::ranges::sort(aux_rows,
                      [&](const Row& a, const Row& b) { return a[pkey_col] < b[pkey_col]; });

    auto aux_it = aux_rows.begin();
    auto aux_end = aux_rows.end();

    std::filesystem::path tmp_path = eng.file_mgr.file_path(fid);
    tmp_path += ".tmp";

    FileId tmp_fid = eng.file_mgr.open_copy(fid, tmp_path);

    {
        SeqPage tmp_aux_page{eng.buf_mgr.fetch_page(tmp_fid, calc_aux_pnum(file_hdr))};
        tmp_aux_page.init();
    }

    pnum_t cur_pnum = 1;

    {
        SeqFile seq_tmp{eng, tmp_fid};
        util::Peekable<Cursor> main_cursor{seq_tmp.cursor()};

        SeqPage cur_page{eng.buf_mgr.fetch_page(fid, cur_pnum)};
        cur_page.init();

        while (aux_it != aux_end || main_cursor.peek()) {
            Row next_row;

            if (aux_it != aux_end &&
                (!main_cursor.peek() ||
                 (*aux_it)[pkey_col] < main_cursor.peek()->get().second[pkey_col])) {
                next_row = *aux_it;
                ++aux_it;
            } else {
                next_row = main_cursor.next()->second;
            }

            if (!cur_page.will_fit(next_row)) {
                cur_pnum += 1;
                cur_page = SeqPage{eng.buf_mgr.fetch_page(fid, cur_pnum)};
                cur_page.init();
            }

            cur_page.push_back(SlotExtra{true}, next_row);
        }
    }

    file_hdr.main_pages = cur_pnum;

    {
        SeqPage new_aux_page{eng.buf_mgr.fetch_page(fid, calc_aux_pnum(file_hdr))};
        new_aux_page.init();
    }

    eng.file_mgr.close(tmp_fid);
    eng.file_mgr.write_user_header(fid, file_hdr);
}

// assumes that `row`'s primary key is not already in the aux page
std::pair<Rid, SeqFile::InsertResult> SeqFile::insert_into_aux(const Row& row) {
    auto file_hdr = eng.file_mgr.read_user_header<SeqHeader>(fid);

    {
        pnum_t aux_pnum = calc_aux_pnum(file_hdr);
        SeqPage aux_page{eng.buf_mgr.fetch_page(fid, aux_pnum)};

        u32 new_slot_idx = aux_page.slot_cnt();

        if (aux_page.will_fit(row)) {
            aux_page.push_back(SlotExtra{true}, row);
            return std::make_pair(Rid{aux_pnum, new_slot_idx}, InsertResult::None);
        }
    }

    rebuild(file_hdr);

    auto [rid, sub_result] = insert_into_aux(row);
    assert(sub_result == InsertResult::None);

    return std::make_pair(rid, InsertResult::Rebuild);
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

using Cursor = SeqFile::Cursor;

Cursor::Cursor(SeqFile& seq_file)
    : seq_file{seq_file},
      seq_hdr{seq_file.eng.file_mgr.read_user_header<SeqHeader>(seq_file.fid)} {}

Cursor SeqFile::cursor() {
    return Cursor{*this};
}

SeqFile::SeqPage& Cursor::page() {
    assert(cur_pnum != PAGE_NIL);

    if (!page_buf)
        page_buf.emplace(seq_file.eng.buf_mgr.fetch_page(seq_file.fid, cur_pnum));

    return *page_buf;
}

std::optional<Cursor::value_type> Cursor::next() {
    while (cur_pnum != PAGE_NIL) {
        if (cur_slot == page().slot_cnt()) {
            page_buf.reset();
            cur_slot = 0;
            cur_pnum += 1;

            if (cur_pnum > calc_aux_pnum(seq_hdr))
                cur_pnum = PAGE_NIL;

            continue;
        }

        if (page().read_slot_extra(cur_slot).active)
            break;

        cur_slot += 1;
    }

    if (cur_pnum == PAGE_NIL)
        return std::nullopt;

    Row row = page().read_data(cur_slot);
    Rid rid{cur_pnum, cur_slot};

    cur_slot += 1;

    return std::make_pair(rid, std::move(row));
}

using RangeCursor = SeqFile::RangeCursor;

SeqFile::SeqPage& RangeCursor::page() {
    assert(cur_pnum != PAGE_NIL);

    if (!page_buf)
        page_buf.emplace(seq_file.eng.buf_mgr.fetch_page(seq_file.fid, cur_pnum));

    return *page_buf;
}

RangeCursor SeqFile::range_search(const Value& pkey_low, const Value& pkey_high) {
    // TODO: start at lowest key >= pkey_low for O(log(n) + k) complexity
    return RangeCursor{*this, pkey_low, pkey_high};
}

RangeCursor::RangeCursor(SeqFile& seq_file, Value pkey_low, Value pkey_high)
    : seq_file{seq_file},
      seq_hdr{seq_file.eng.file_mgr.read_user_header<SeqHeader>(seq_file.fid)},
      pkey_low(std::move(pkey_low)),
      pkey_high{std::move(pkey_high)} {}

std::optional<RangeCursor::value_type> RangeCursor::next() {
    while (cur_pnum != PAGE_NIL) {
        if (cur_slot == page().slot_cnt()) {
            page_buf.reset();
            cur_slot = 0;
            cur_pnum += 1;

            if (cur_pnum > calc_aux_pnum(seq_hdr))
                cur_pnum = PAGE_NIL;

            continue;
        }

        auto extra = page().read_slot_extra(cur_slot);

        if (extra.active) {
            Row row = page().read_data(cur_slot);
            if (row[seq_hdr.pkey_col] >= pkey_low && row[seq_hdr.pkey_col] <= pkey_high)
                break;
        }

        cur_slot += 1;
    }

    if (cur_pnum == PAGE_NIL)
        return std::nullopt;

    Row row = page().read_data(cur_slot);
    Rid rid{cur_pnum, cur_slot};

    cur_slot += 1;

    return std::make_pair(rid, row);
    ;
}
