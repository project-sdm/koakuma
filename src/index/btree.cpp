#include "index/btree.hpp"
#include <cassert>
#include <cstddef>
#include <optional>
#include <print>
#include <stack>
#include <tuple>
#include <utility>
#include "engine/file_manager.hpp"
#include "pack.hpp"
#include "seq_file.hpp"
#include "util.hpp"

BTreeIndex::Slot::Slot(u32 pos, u32 len, pnum_t left_child)
    : pos{pos},
      len{len},
      left_child{left_child} {}

BTreeIndex::Slot::Slot(u32 pos, u32 len, Rid rid)
    : pos{pos},
      len{len},
      rid{rid} {}

BTreeIndex::BTreeIndex(Engine& engine, FileId fid)
    : eng{engine},
      fid{fid} {}

void BTreeIndex::init() {
    // init header
    BTreeHeader file_hdr{};
    file_hdr.root = eng.file_mgr.alloc_page(fid);
    eng.file_mgr.write_user_header(fid, file_hdr);

    // init root node
    auto root_page = eng.buf_mgr.fetch_page(fid, file_hdr.root);
    util::span_write(root_page.data(), 0, PageHeader{});
}

bool BTreeIndex::is_leaf(const PageHeader& hdr) {
    return hdr.last_child == PAGE_NIL;
}

std::optional<Rid> BTreeIndex::search(const Value& pkey) {
    auto file_hdr = eng.file_mgr.read_user_header<BTreeHeader>(fid);
    assert(file_hdr.root != PAGE_NIL);  // should exist due to init()

    pnum_t cur_page = file_hdr.root;

    while (true) {
        SlottedPage page{eng.buf_mgr.fetch_page(fid, cur_page)};

        if (is_leaf(page.header()))
            break;

        for (std::size_t i = 0; i < page.slot_cnt(); ++i) {
            Slot slot = page.read_slot(i);

            if (pkey < page.read_key(slot)) {
                cur_page = slot.left_child;
                goto next_level;
            }
        }

        cur_page = page.header().last_child;
    next_level:
    }

    SlottedPage leaf_page{eng.buf_mgr.fetch_page(fid, cur_page)};

    for (std::size_t i = 0; i < leaf_page.slot_cnt(); ++i) {
        Slot slot = leaf_page.read_slot(i);

        if (pkey == leaf_page.read_key(slot))
            return slot.rid;
    }

    return std::nullopt;
}

BTreeIndex::SlottedPage::SlottedPage(PageGuard page)
    : page{std::move(page)},
      hdr{util::span_read<PageHeader>(page.const_data(), 0)} {}

BTreeIndex::SlottedPage::~SlottedPage() {
    // TODO: only do this when something has actually changed
    util::span_write(page.data(), 0, hdr);
}

u32 BTreeIndex::SlottedPage::total_space() const {
    return page.const_data().size() - sizeof(PageHeader);
}

u32 BTreeIndex::SlottedPage::free_space() const {
    // prevents an underflow that should not happen
    assert(hdr.data_begin >= sizeof(PageHeader) + (hdr.slot_cnt * sizeof(Slot)));

    return hdr.data_begin - sizeof(PageHeader) - (hdr.slot_cnt * sizeof(Slot));
}

u32 BTreeIndex::Slot::data_pos() const {
    return pos;
}

void BTreeIndex::SlottedPage::init() {
    hdr = PageHeader{};
}

const BTreeIndex::PageHeader& BTreeIndex::SlottedPage::header() const {
    return hdr;
}

BTreeIndex::PageHeader& BTreeIndex::SlottedPage::header() {
    return hdr;
}

u32 BTreeIndex::SlottedPage::slot_cnt() const {
    return hdr.slot_cnt;
}

u32 BTreeIndex::SlottedPage::slot_offset(u32 slot_idx) {
    return sizeof(PageHeader) + (slot_idx * sizeof(Slot));
}

BTreeIndex::Slot BTreeIndex::SlottedPage::read_slot(u32 slot_idx) const {
    assert(slot_idx < hdr.slot_cnt);
    return util::span_read<Slot>(page.const_data(), slot_offset(slot_idx));
}

void BTreeIndex::SlottedPage::write_slot(u32 slot_idx, const Slot& slot) {
    util::span_write(page.data(), slot_offset(slot_idx), slot);
}

Value BTreeIndex::SlottedPage::read_key(const Slot& slot) const {
    return pack::unpack_alloc<Value>(page.const_data().subspan(slot.data_pos()).data());
}

Value BTreeIndex::SlottedPage::read_key(u32 slot_idx) const {
    Slot slot = read_slot(slot_idx);
    return read_key(slot);
}

bool BTreeIndex::SlottedPage::will_fit(const Value& pkey) const {
    return free_space() >= sizeof(Slot) + pack::pack_size(pkey);
}

void BTreeIndex::SlottedPage::inner_push_back(const Value& pkey, pnum_t left_child) {
    push_back<pnum_t>(pkey, left_child);
}

void BTreeIndex::SlottedPage::inner_insert(u32 slot_idx, const Value& pkey, pnum_t left_child) {
    insert<pnum_t>(slot_idx, pkey, left_child);
}

void BTreeIndex::SlottedPage::leaf_push_back(const Value& pkey, Rid rid) {
    push_back<Rid>(pkey, rid);
}

void BTreeIndex::SlottedPage::leaf_insert(u32 slot_idx, const Value& pkey, Rid rid) {
    insert<Rid>(slot_idx, pkey, rid);
}

// og_pnum is freed and becomes invalid
std::pair<pnum_t, pnum_t> BTreeIndex::leaf_split(pnum_t og_pnum,
                                                 u32 ins_idx,
                                                 const Value& ins_pkey,
                                                 Rid ins_rid) {
    SlottedPage og_page{eng.buf_mgr.fetch_page(fid, og_pnum)};

    pnum_t left_pnum = eng.file_mgr.alloc_page(fid);
    pnum_t right_pnum = eng.file_mgr.alloc_page(fid);

    // PERF: could move these a bit further down each
    SlottedPage left_page{eng.buf_mgr.fetch_page(fid, left_pnum)};
    SlottedPage right_page{eng.buf_mgr.fetch_page(fid, right_pnum)};

    // because these are newly allocated pages
    left_page.init();
    right_page.init();

    left_page.header().next_page = right_pnum;
    left_page.header().prev_page = og_page.header().prev_page;

    right_page.header().next_page = og_page.header().next_page;
    right_page.header().prev_page = left_pnum;

    u32 src_slot = 0;
    bool new_slot_pending = true;

    while (left_page.free_space() > left_page.total_space() / 2) {
        if (new_slot_pending && src_slot == ins_idx) {
            left_page.leaf_push_back(ins_pkey, ins_rid);
            new_slot_pending = false;
        } else {
            assert(src_slot < og_page.slot_cnt());

            // PERF: we could void de/re-serialization with std::memcpy or std::copy
            Slot slot = og_page.read_slot(src_slot);
            Value slot_pkey = og_page.read_key(slot);

            left_page.leaf_push_back(slot_pkey, slot.rid);
            src_slot += 1;
        }
    }

    // TODO: deduplicate this block with the previous near-identical block
    while (src_slot < og_page.slot_cnt() || new_slot_pending) {
        if (new_slot_pending && src_slot == ins_idx) {
            right_page.leaf_push_back(ins_pkey, ins_rid);
            new_slot_pending = false;
        } else {
            assert(src_slot < og_page.slot_cnt());

            // PERF: we could void de/re-serialization with std::memcpy or std::copy
            Slot slot = og_page.read_slot(src_slot);
            Value slot_pkey = og_page.read_key(slot);

            right_page.leaf_push_back(slot_pkey, slot.rid);
            src_slot += 1;
        }
    }

    eng.file_mgr.free_page(fid, og_pnum);
    return {left_pnum, right_pnum};
}

std::pair<u32, pnum_t> BTreeIndex::inner_find_child(const SlottedPage& page, const Value& pkey) {
    assert(!is_leaf(page.header()));
    u32 fit_idx = 0;

    for (; fit_idx < page.slot_cnt(); ++fit_idx) {
        Slot slot = page.read_slot(fit_idx);

        if (pkey < page.read_key(slot)) {
            return {fit_idx, slot.left_child};
        }
    }

    return {fit_idx, page.header().last_child};
}

u32 BTreeIndex::leaf_lower_bound_idx(const SlottedPage& page, const Value& pkey) {
    assert(is_leaf(page.header()));

    u32 ins_idx = 0;

    for (; ins_idx < page.slot_cnt(); ++ins_idx) {
        Slot slot = page.read_slot(ins_idx);
        auto key = page.read_key(slot);

        if (pkey <= key)
            break;
    }

    return ins_idx;
}

bool BTreeIndex::insert(const Value& pkey, Rid rid) {
    auto file_hdr = eng.file_mgr.read_user_header<BTreeHeader>(fid);
    assert(file_hdr.root != PAGE_NIL);  // should exist due to init()

    pnum_t cur_page = file_hdr.root;
    std::stack<std::pair<pnum_t, u32>> path;

    while (true) {
        SlottedPage page{eng.buf_mgr.fetch_page(fid, cur_page)};
        if (is_leaf(page.header()))
            break;

        auto [fit_idx, fit_pnum] = inner_find_child(page, pkey);

        path.emplace(cur_page, fit_idx);
        cur_page = fit_pnum;
    }

    pnum_t leaf_pnum = cur_page;
    SlottedPage leaf_page{eng.buf_mgr.fetch_page(fid, leaf_pnum)};

    u32 ins_idx = leaf_lower_bound_idx(leaf_page, pkey);

    if (ins_idx < leaf_page.slot_cnt() && leaf_page.read_key(ins_idx) == pkey)
        return false;

    if (leaf_page.will_fit(pkey)) {
        leaf_page.leaf_insert(ins_idx, pkey, rid);
        return true;
    }

    auto [left_pnum, right_pnum] = leaf_split(leaf_pnum, ins_idx, pkey, rid);

    std::optional<std::tuple<pnum_t, Value, pnum_t>> lifted;
    {
        SlottedPage right_page{eng.buf_mgr.fetch_page(fid, right_pnum)};
        lifted = std::make_tuple(left_pnum, right_page.read_key(0), right_pnum);
    }

    while (lifted && !path.empty()) {
        auto [pnum, idx] = path.top();
        path.pop();

        SlottedPage page{eng.buf_mgr.fetch_page(fid, pnum)};

        auto [left_pnum, right_min, right_pnum] = *std::move(lifted);
        lifted.reset();

        if (idx == page.slot_cnt()) {
            page.header().last_child = left_pnum;
        } else {
            auto og_slot = page.read_slot(idx);
            og_slot.left_child = left_pnum;
            page.write_slot(idx, og_slot);
        }

        if (page.will_fit(right_min)) {
            page.inner_insert(idx, right_min, right_pnum);
        } else {
            // auto [new_left_pnum, new_right_pnum] = inner_split(pnum, idx, pkey, right_pnum);

            // TODO: how do i get the min of the right split
            // lifted = std::make_tuple(new_left_pnum, _, new_right_pnum);
        }
    }

    if (lifted) {
        // topmost edge case: make a new root node for final split
        auto [left_pnum, right_min, right_pnum] = *std::move(lifted);
        lifted.reset();

        pnum_t new_root_pnum = eng.file_mgr.alloc_page(fid);
        SlottedPage new_root_page{eng.buf_mgr.fetch_page(fid, new_root_pnum)};

        new_root_page.init();

        new_root_page.header().last_child = right_pnum;
        new_root_page.inner_insert(0, right_min, left_pnum);

        file_hdr.root = new_root_pnum;
        eng.file_mgr.write_user_header(fid, file_hdr);
    }

    SlottedPage root_page{eng.buf_mgr.fetch_page(fid, file_hdr.root)};

    for (u32 i = 0; i < root_page.slot_cnt(); ++i) {
        auto slot = root_page.read_slot(i);
        std::print("{} ", root_page.read_key(slot));
    }
    std::println();

    return true;
}
