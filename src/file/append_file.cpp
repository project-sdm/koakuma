#include "file/append_file.hpp"
#include <utility>
#include "file/common.hpp"

AppendHeader::AppendHeader() = default;

AppendHeader::AppendHeader(std::vector<Column> columns)
    : hdr{FileType::Append, std::move(columns)} {}

AppendFile::AppendFile(Engine& engine, FileId fid)
    : eng{engine},
      fid{fid} {}

void AppendFile::init(std::vector<Column> columns) {
    eng.file_mgr.init_file(fid);

    {
        AppendPage first_page{eng.buf_mgr.fetch_page(fid, 1)};
        first_page.init();
    }

    AppendHeader file_hdr{std::move(columns)};
    file_hdr.used_pages = 1;
    eng.file_mgr.write_user_header(fid, file_hdr);
}

Row AppendFile::read_rid(Rid rid) const {
    AppendPage page{eng.buf_mgr.fetch_page(fid, rid.pnum)};
    return page.read_data(rid.slot_idx);
}

Rid AppendFile::add(const Row& row) {
    auto file_hdr = eng.file_mgr.read_user_header<AppendHeader>(fid);

    AppendPage page{eng.buf_mgr.fetch_page(fid, file_hdr.used_pages)};

    if (!page.will_fit(row)) {
        file_hdr.used_pages += 1;
        eng.file_mgr.write_user_header(fid, file_hdr);

        page = AppendPage{eng.buf_mgr.fetch_page(fid, file_hdr.used_pages)};
        page.init();
    }

    page.push_back({}, row);
    return {file_hdr.used_pages, page.slot_cnt() - 1};
}

using Cursor = AppendFile::Cursor;

Cursor::Cursor(AppendFile& append_file)
    : append_file{append_file},
      append_hdr{append_file.eng.file_mgr.read_user_header<AppendHeader>(append_file.fid)} {}

Cursor AppendFile::cursor() {
    return Cursor{*this};
}

AppendFile::AppendPage& Cursor::page() {
    assert(cur_pnum != PAGE_NIL);

    if (!page_buf)
        page_buf.emplace(append_file.eng.buf_mgr.fetch_page(append_file.fid, cur_pnum));

    return *page_buf;
}

std::optional<Cursor::value_type> Cursor::next() {
    if (cur_pnum != PAGE_NIL && cur_slot == page().slot_cnt()) {
        page_buf.reset();
        cur_slot = 0;
        cur_pnum += 1;

        if (cur_pnum > append_hdr.used_pages)
            cur_pnum = PAGE_NIL;
    }

    if (cur_pnum == PAGE_NIL)
        return std::nullopt;

    Row row = page().read_data(cur_slot);
    Rid rid{cur_pnum, cur_slot};

    cur_slot += 1;

    return std::make_pair(rid, std::move(row));
}
