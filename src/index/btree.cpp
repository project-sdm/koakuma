#include "index/btree.hpp"
#include <cassert>
#include <optional>
#include <print>
#include <queue>
#include <stack>
#include <tuple>
#include <utility>
#include "engine/buffer_manager.hpp"
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

    for (u32 i = 0; i < leaf_page.slot_cnt(); ++i) {
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

    auto og_hdr = og_page.const_header_extra();

    if (og_hdr.prev_page != PAGE_NIL) {
        NodePage og_left_page{eng.buf_mgr.fetch_page(fid, og_hdr.prev_page)};
        og_left_page.header_extra().next_page = left_pnum;
    }

    if (og_hdr.next_page != PAGE_NIL) {
        NodePage og_right_page{eng.buf_mgr.fetch_page(fid, og_hdr.next_page)};
        og_right_page.header_extra().next_page = right_pnum;
    }

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

bool BTreeIndex::remove(const Value& pkey) {
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
    std::optional<Value> updated_min = std::nullopt;

    {
        NodePage leaf_page{eng.buf_mgr.fetch_page(fid, leaf_pnum)};
        u32 i = 0;

        for (; i < leaf_page.slot_cnt(); ++i) {
            auto slot = leaf_page.read_slot(i);

            if (pkey == leaf_page.read_data(slot))
                break;
        }

        assert(i <= leaf_page.slot_cnt());

        if (i == leaf_page.slot_cnt())
            return false;

        auto removed_pkey = leaf_page.read_data(i);
        leaf_page.remove(i);

        updated_min = leaf_page.read_data(0);

        // first path.pop() is an edge case for leaves.
        // we do it here to use the existing leaf_page object
    }

    if (!path.empty()) {
        auto [par_pnum, par_idx] = path.top();
        path.pop();

        NodePage par_page{eng.buf_mgr.fetch_page(fid, par_pnum)};

        pnum_t leaf_pnum = child_pnum(par_page, par_idx);
        std::optional<pnum_t> to_free = std::nullopt;

        {
            NodePage leaf_page{eng.buf_mgr.fetch_page(fid, leaf_pnum)};

            if (par_idx > 0) {
                par_page.update_data(par_idx - 1, *updated_min);
                updated_min = std::nullopt;
            }

            if (leaf_page.used_space() < leaf_page.total_space() / 2) {
                // borrow if possible, otherwise merge (with prev if possible, otherwise with next)
                if (!leaf_try_borrow(leaf_page, par_page, par_idx)) {
                    auto leaf_hdr_extra = leaf_page.const_header_extra();

                    if (leaf_hdr_extra.prev_page != PAGE_NIL) {
                        // merge leaf onto prev
                        to_free = leaf_pnum;

                        {
                            NodePage prev_page{
                                eng.buf_mgr.fetch_page(fid, leaf_hdr_extra.prev_page)};

                            for (u32 i = 0; i < leaf_page.slot_cnt(); ++i) {
                                auto slot = leaf_page.read_slot(i);
                                Value slot_pkey = leaf_page.read_data(slot);
                                prev_page.push_back(slot.extra(), slot_pkey);
                            }
                        }

                        if (par_idx < par_page.slot_cnt() - 1) {
                            assert(par_idx > 0);

                            Value shifted_key = par_page.read_data(par_idx);
                            par_page.remove(par_idx);
                            par_page.update_data(par_idx - 1, shifted_key);
                        } else {
                            assert(false && "TODO: test this");
                            par_page.remove(par_page.slot_cnt() - 1);
                            par_page.header_extra().last_child = leaf_hdr_extra.prev_page;
                        }
                    } else if (leaf_hdr_extra.next_page != PAGE_NIL) {
                        // merge next onto leaf
                        to_free = leaf_hdr_extra.next_page;

                        {
                            NodePage next_page{
                                eng.buf_mgr.fetch_page(fid, leaf_hdr_extra.next_page)};

                            for (u32 i = 0; i < next_page.slot_cnt(); ++i) {
                                auto slot = next_page.read_slot(i);
                                Value slot_pkey = next_page.read_data(slot);
                                leaf_page.push_back(slot.extra(), slot_pkey);
                            }
                        }

                        if (par_idx < par_page.slot_cnt() - 1) {
                            Value shifted_key = par_page.read_data(par_idx + 1);
                            par_page.remove(par_idx + 1);
                            par_page.update_data(par_idx, shifted_key);
                        } else {
                            assert(false && "TODO: test this");
                            par_page.remove(par_page.slot_cnt() - 1);
                            par_page.header_extra().last_child = leaf_pnum;
                        }
                    }
                }
            }
        }

        if (to_free)
            eng.file_mgr.free_page(fid, *to_free);
    }

    while (!path.empty()) {
        auto [pnum, idx] = path.top();
        path.pop();

        if (updated_min) {
            NodePage page{eng.buf_mgr.fetch_page(fid, pnum)};

            if (idx > 0) {
                page.update_data(idx - 1, *updated_min);
                updated_min = std::nullopt;  // this did not update the current subtree's min key
            }
        }

        assert(false);
    }

    return true;
}

bool BTreeIndex::leaf_try_borrow(NodePage& leaf_page, NodePage& par_page, u32 par_idx) {
    auto hdr_extra = leaf_page.const_header_extra();

    if (hdr_extra.prev_page != PAGE_NIL) {
        NodePage prev_page{eng.buf_mgr.fetch_page(fid, hdr_extra.prev_page)};
        auto last_slot = prev_page.read_slot(prev_page.slot_cnt() - 1);

        if (prev_page.used_space() >= prev_page.total_space() / 2) {
            Value borrowed = prev_page.read_data(last_slot);
            prev_page.remove(prev_page.slot_cnt() - 1);

            leaf_page.insert(0, SlotExtra::leaf(last_slot.extra().rid), borrowed);

            assert(par_idx > 0);
            par_page.update_data(par_idx - 1, borrowed);
            return true;
        }
    }

    if (hdr_extra.next_page != PAGE_NIL) {
        NodePage next_page{eng.buf_mgr.fetch_page(fid, hdr_extra.next_page)};
        auto fst_slot = next_page.read_slot(0);

        if (next_page.used_space() >= next_page.total_space() / 2) {
            Value borrowed = next_page.read_data(fst_slot);
            next_page.remove(0);

            leaf_page.push_back(SlotExtra::leaf(fst_slot.extra().rid), borrowed);

            Value new_right_min = next_page.read_data(0);
            par_page.update_data(par_idx, new_right_min);

            return true;
        }
    }

    return false;
}

[[nodiscard]] pnum_t BTreeIndex::child_pnum(const NodePage& page, u32 child_idx) {
    if (child_idx == page.slot_cnt())
        return page.const_header_extra().last_child;

    auto slot = page.read_slot(child_idx);
    return slot.extra().left_child;
}

using RangeCursor = BTreeIndex::RangeCursor;

RangeCursor::RangeCursor(BufferManager& buf_mgr, pnum_t init_page, u32 init_slot, Value pkey_high)
    : buf_mgr{buf_mgr},
      cur_pnum{init_page},
      cur_slot{init_slot},
      pkey_high{std::move(pkey_high)} {}

RangeCursor::RangeCursor(BufferManager& buf_mgr)
    : buf_mgr{buf_mgr},
      cur_pnum{0},
      cur_slot{0},
      finished{true} {}

std::optional<Rid> RangeCursor::next() {
    if (finished) {
        assert(!page);
        return std::nullopt;
    }

    if (!page) {
        if (cur_pnum == PAGE_NIL) {
            finished = true;
            return std::nullopt;
        }

        page = NodePage{buf_mgr.fetch_page(fid, cur_pnum)};
    }

    if (cur_slot == page->slot_cnt()) {
        cur_slot = 0;
        cur_pnum = page->const_header_extra().next_page;
        page = std::nullopt;
        return next();
    }

    auto slot = page->read_slot(cur_slot);
    Value key = page->read_data(slot);

    if (key > pkey_high) {
        finished = true;
        page = std::nullopt;
        return std::nullopt;
    }

    cur_slot += 1;

    return slot.extra().rid;
}

RangeCursor BTreeIndex::range_search(const Value& pkey_low, const Value& pkey_high) {
    auto file_hdr = eng.file_mgr.read_user_header<BTreeHeader>(fid);
    assert(file_hdr.root != PAGE_NIL);  // should exist due to init()

    pnum_t cur_page = file_hdr.root;

    while (true) {
        NodePage page{eng.buf_mgr.fetch_page(fid, cur_page)};
        if (is_leaf(page.const_header_extra()))
            break;

        auto [_, fit_child] = inner_find_child(page, pkey_low);
        cur_page = fit_child;
    }

    NodePage leaf_page{eng.buf_mgr.fetch_page(fid, cur_page)};

    u32 start_slot = 0;

    for (; start_slot < leaf_page.slot_cnt(); ++start_slot) {
        auto slot = leaf_page.read_slot(start_slot);

        if (leaf_page.read_data(slot) >= pkey_low)
            break;
    }

    return RangeCursor{eng.buf_mgr, cur_page, start_slot, pkey_high};
}
