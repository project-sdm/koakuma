#include "index/btree.hpp"
#include <cassert>
#include <cstddef>
#include <optional>
#include <print>
#include <queue>
#include <stack>
#include <tuple>
#include <utility>
#include "engine/file_manager.hpp"
#include "seq_file.hpp"

BTreeIndex::BTreeIndex(Engine& engine, FileId fid)
    : eng{engine},
      fid{fid} {}

void BTreeIndex::init() {
    // init header
    BTreeHeader file_hdr{};
    file_hdr.root = eng.file_mgr.alloc_page(fid);
    eng.file_mgr.write_user_header(fid, file_hdr);

    NodePage root_page{eng.buf_mgr.fetch_page(fid, file_hdr.root)};
    root_page.init();
}

bool BTreeIndex::is_leaf(const NodeExtra& node) {
    return node.last_child == PAGE_NIL;
}

std::optional<Rid> BTreeIndex::search(const Value& pkey) {
    auto file_hdr = eng.file_mgr.read_user_header<BTreeHeader>(fid);
    assert(file_hdr.root != PAGE_NIL);  // should exist due to init()

    pnum_t cur_page = file_hdr.root;

    while (true) {
        NodePage page{eng.buf_mgr.fetch_page(fid, cur_page)};
        if (is_leaf(page.const_header_extra()))
            break;

        auto [_, fit_child] = inner_find_child(page, pkey);
        cur_page = fit_child;
    }

    NodePage leaf_page{eng.buf_mgr.fetch_page(fid, cur_page)};

    for (std::size_t i = 0; i < leaf_page.slot_cnt(); ++i) {
        auto slot = leaf_page.read_slot(i);

        if (pkey == leaf_page.read_data(slot))
            return slot.extra().rid;
    }

    return std::nullopt;
}

// og_pnum is freed and becomes invalid
std::pair<pnum_t, pnum_t> BTreeIndex::leaf_insert_split(pnum_t og_pnum,
                                                        u32 ins_idx,
                                                        const Value& ins_pkey,
                                                        Rid ins_rid) {
    NodePage og_page{eng.buf_mgr.fetch_page(fid, og_pnum)};

    u32 src_slot = 0;
    bool new_slot_pending = true;

    auto entry_pending = [&]() { return src_slot < og_page.slot_cnt() || new_slot_pending; };

    auto take_entry = [&]() {
        if (new_slot_pending && src_slot == ins_idx) {
            new_slot_pending = false;
            return std::make_pair(ins_pkey, ins_rid);
        }

        assert(src_slot < og_page.slot_cnt());

        auto slot = og_page.read_slot(src_slot);
        Value slot_pkey = og_page.read_data(slot);
        src_slot += 1;

        return std::make_pair(slot_pkey, slot.extra().rid);
    };

    pnum_t left_pnum = eng.file_mgr.alloc_page(fid);
    pnum_t right_pnum = eng.file_mgr.alloc_page(fid);

    {
        NodePage left_page{eng.buf_mgr.fetch_page(fid, left_pnum)};

        left_page.init();
        left_page.header_extra().next_page = right_pnum;
        left_page.header_extra().prev_page = og_page.const_header_extra().prev_page;

        while (left_page.free_space() > left_page.total_space() / 2) {
            auto [pkey, rid] = take_entry();
            left_page.push_back(SlotExtra::leaf(rid), pkey);
        }
    }

    {
        NodePage right_page{eng.buf_mgr.fetch_page(fid, right_pnum)};

        right_page.init();
        right_page.header_extra().next_page = og_page.const_header_extra().next_page;
        right_page.header_extra().prev_page = left_pnum;

        while (entry_pending()) {
            auto [pkey, rid] = take_entry();
            right_page.push_back(SlotExtra::leaf(rid), pkey);
        }
    }

    return {left_pnum, right_pnum};
}

std::tuple<pnum_t, Value, pnum_t> BTreeIndex::inner_insert_split(pnum_t og_pnum,
                                                                 u32 ins_idx,
                                                                 const Value& ins_pkey,
                                                                 pnum_t ins_left_child) {
    NodePage og_page{eng.buf_mgr.fetch_page(fid, og_pnum)};

    pnum_t left_pnum = eng.file_mgr.alloc_page(fid);
    pnum_t right_pnum = eng.file_mgr.alloc_page(fid);

    u32 src_slot = 0;
    bool mid_pending = true;

    auto take_entry = [&]() {
        if (mid_pending && src_slot == ins_idx) {
            mid_pending = false;
            return std::make_pair(ins_pkey, ins_left_child);
        }

        assert(src_slot < og_page.slot_cnt());

        auto slot = og_page.read_slot(src_slot);
        Value slot_pkey = og_page.read_data(slot);
        src_slot += 1;

        return std::make_pair(slot_pkey, slot.extra().left_child);
    };

    Value mid_pkey;

    {
        NodePage left_page{eng.buf_mgr.fetch_page(fid, left_pnum)};

        left_page.init();
        left_page.header_extra().next_page = right_pnum;
        left_page.header_extra().prev_page = og_page.const_header_extra().prev_page;

        while (left_page.free_space() > left_page.total_space() / 2) {
            auto [pkey, left_child] = take_entry();
            left_page.push_back(SlotExtra::inner(left_child), pkey);
        }

        auto [next_pkey, next_left_child] = take_entry();

        mid_pkey = std::move(next_pkey);
        left_page.header_extra().last_child = next_left_child;
    }

    {
        NodePage right_page{eng.buf_mgr.fetch_page(fid, right_pnum)};

        right_page.init();
        right_page.header_extra().next_page = og_page.const_header_extra().next_page;
        right_page.header_extra().prev_page = left_pnum;

        while (src_slot < og_page.slot_cnt() || mid_pending) {
            auto [pkey, left_child] = take_entry();
            right_page.push_back(SlotExtra{left_child}, pkey);
        }

        right_page.header_extra().last_child = og_page.const_header_extra().last_child;
    }

    return {left_pnum, mid_pkey, right_pnum};
}

std::pair<u32, pnum_t> BTreeIndex::inner_find_child(const NodePage& page, const Value& pkey) {
    assert(!is_leaf(page.const_header_extra()));
    u32 fit_idx = 0;

    for (; fit_idx < page.slot_cnt(); ++fit_idx) {
        auto slot = page.read_slot(fit_idx);

        if (pkey < page.read_data(slot)) {
            return {fit_idx, slot.extra().left_child};
        }
    }

    return {fit_idx, page.const_header_extra().last_child};
}

u32 BTreeIndex::leaf_lower_bound_idx(const NodePage& page, const Value& pkey) {
    assert(is_leaf(page.const_header_extra()));

    u32 ins_idx = 0;

    for (; ins_idx < page.slot_cnt(); ++ins_idx) {
        auto slot = page.read_slot(ins_idx);
        auto key = page.read_data(slot);

        if (pkey <= key)
            break;
    }

    return ins_idx;
}

bool BTreeIndex::add(const Value& pkey, Rid rid) {
    auto file_hdr = eng.file_mgr.read_user_header<BTreeHeader>(fid);

    assert(file_hdr.root != PAGE_NIL);  // should exist due to init()

    pnum_t cur_page = file_hdr.root;
    std::stack<std::pair<pnum_t, u32>> path;

    while (true) {
        NodePage page{eng.buf_mgr.fetch_page(fid, cur_page)};
        if (is_leaf(page.const_header_extra()))
            break;

        auto [fit_idx, fit_child] = inner_find_child(page, pkey);

        path.emplace(cur_page, fit_idx);
        cur_page = fit_child;
    }

    pnum_t leaf_pnum = cur_page;

    u32 ins_idx = 0;

    {
        NodePage leaf_page{eng.buf_mgr.fetch_page(fid, leaf_pnum)};

        ins_idx = leaf_lower_bound_idx(leaf_page, pkey);

        if (ins_idx < leaf_page.slot_cnt() && leaf_page.read_data(ins_idx) == pkey)
            return false;

        if (leaf_page.will_fit(pkey)) {
            // a non-overflowing insert NEVER causes inner node updates. I have
            // discovered a truly marvelous proof of this, which this comment
            // is too narrow to contain.
            leaf_page.insert(ins_idx, SlotExtra::leaf(rid), pkey);
            return true;
        }
    }

    auto [left_pnum, right_pnum] = leaf_insert_split(leaf_pnum, ins_idx, pkey, rid);
    eng.file_mgr.free_page(fid, leaf_pnum);

    std::optional<std::tuple<pnum_t, Value, pnum_t>> lifted;
    {
        NodePage right_page{eng.buf_mgr.fetch_page(fid, right_pnum)};
        lifted = {left_pnum, right_page.read_data(0), right_pnum};
    }

    while (lifted && !path.empty()) {
        auto [pnum, idx] = path.top();
        path.pop();

        auto [left_pnum, right_min, right_pnum] = *std::move(lifted);
        lifted.reset();

        bool split = false;

        {
            NodePage page{eng.buf_mgr.fetch_page(fid, pnum)};

            // this takes care of using `right_pnum`
            if (idx == page.slot_cnt()) {
                page.header_extra().last_child = right_pnum;
            } else {
                auto og_slot = page.read_slot(idx);
                og_slot.extra().left_child = right_pnum;
                page.write_slot(idx, og_slot);
            }

            // all that's left is inserting `right_min` and its left child, `left_pnum`
            if (page.will_fit(right_min))
                page.insert(idx, SlotExtra::inner(left_pnum), right_min);
            else
                split = true;
        }

        // we need to split outside the scope of `page` as to now have any
        // alive `NodePage` when calling free_page().
        if (split) {
            lifted = inner_insert_split(pnum, idx, right_min, left_pnum);
            eng.file_mgr.free_page(fid, pnum);
        }
    }

    if (lifted) {
        // topmost edge case: make a new root node for final split
        auto [left_pnum, right_min, right_pnum] = *std::move(lifted);
        lifted.reset();

        pnum_t new_root_pnum = eng.file_mgr.alloc_page(fid);

        {
            NodePage new_root_page{eng.buf_mgr.fetch_page(fid, new_root_pnum)};

            new_root_page.init();
            new_root_page.insert(0, SlotExtra::inner(left_pnum), right_min);
            new_root_page.header_extra().last_child = right_pnum;
        }

        file_hdr.root = new_root_pnum;
        eng.file_mgr.write_user_header(fid, file_hdr);
    }

    return true;
}

void BTreeIndex::ugly_print() const {
    auto file_hdr = eng.file_mgr.read_user_header<BTreeHeader>(fid);

    int last_depth = 0;
    std::queue<std::pair<pnum_t, int>> q;
    q.emplace(file_hdr.root, 0);

    while (!q.empty()) {
        auto [pnum, depth] = q.front();
        q.pop();

        if (depth != last_depth)
            std::println();

        last_depth = depth;

        NodePage page{eng.buf_mgr.fetch_page(fid, pnum)};

        for (u32 i = 0; i < page.slot_cnt(); ++i) {
            auto slot = page.read_slot(i);
            std::print("{} ", page.read_data(slot));

            if (!is_leaf(page.const_header_extra()))
                q.emplace(slot.extra().left_child, depth + 1);
        }
        std::print("     ");

        if (!is_leaf(page.const_header_extra()))
            q.emplace(page.const_header_extra().last_child, depth + 1);
    }

    std::println();
}
