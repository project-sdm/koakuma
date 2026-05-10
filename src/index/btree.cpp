#include "index/btree.hpp"
#include <cassert>
#include <cstddef>
#include <optional>
#include <print>
#include <queue>
#include <stack>
#include <tuple>
#include <utility>
#include "engine/buffer_manager.hpp"
#include "engine/file_manager.hpp"
#include "file/common.hpp"

BTreeIndex::BTreeIndex(Engine& engine, FileId fid)
    : eng{engine},
      fid{fid} {}

void BTreeIndex::init() {
    eng.file_mgr.init_file(fid);

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

BTreeIndex::RangeCursor BTreeIndex::search(const Value& key) {
    return range_search(key, key);
}

// og_pnum is freed and becomes invalid
std::pair<pnum_t, pnum_t> BTreeIndex::leaf_insert_split(pnum_t og_pnum,
                                                        u32 ins_idx,
                                                        const Value& ins_key,
                                                        Rid ins_rid) {
    NodePage og_page{eng.buf_mgr.fetch_page(fid, og_pnum)};

    u32 src_slot = 0;
    bool new_slot_pending = true;

    auto entry_pending = [&]() { return src_slot < og_page.slot_cnt() || new_slot_pending; };

    auto take_entry = [&]() {
        if (new_slot_pending && src_slot == ins_idx) {
            new_slot_pending = false;
            return std::make_pair(ins_key, ins_rid);
        }

        assert(src_slot < og_page.slot_cnt());

        Value slot_key = og_page.read_data(src_slot);
        auto extra = og_page.read_slot_extra(src_slot);
        src_slot += 1;

        return std::make_pair(slot_key, extra.rid);
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
            auto [key, rid] = take_entry();
            left_page.push_back(SlotExtra::leaf(rid), key);
        }
    }

    {
        NodePage right_page{eng.buf_mgr.fetch_page(fid, right_pnum)};

        right_page.init();
        right_page.header_extra().next_page = og_page.const_header_extra().next_page;
        right_page.header_extra().prev_page = left_pnum;

        while (entry_pending()) {
            auto [key, rid] = take_entry();
            right_page.push_back(SlotExtra::leaf(rid), key);
        }
    }

    return {left_pnum, right_pnum};
}

std::tuple<pnum_t, Value, pnum_t> BTreeIndex::inner_insert_split(pnum_t og_pnum,
                                                                 u32 ins_idx,
                                                                 const Value& ins_key,
                                                                 pnum_t ins_left_child) {
    NodePage og_page{eng.buf_mgr.fetch_page(fid, og_pnum)};

    pnum_t left_pnum = eng.file_mgr.alloc_page(fid);
    pnum_t right_pnum = eng.file_mgr.alloc_page(fid);

    u32 src_slot = 0;
    bool mid_pending = true;

    auto take_entry = [&]() {
        if (mid_pending && src_slot == ins_idx) {
            mid_pending = false;
            return std::make_pair(ins_key, ins_left_child);
        }

        assert(src_slot < og_page.slot_cnt());

        Value slot_key = og_page.read_data(src_slot);
        auto extra = og_page.read_slot_extra(src_slot);
        src_slot += 1;

        return std::make_pair(slot_key, extra.left_child);
    };

    Value mid_key;

    {
        NodePage left_page{eng.buf_mgr.fetch_page(fid, left_pnum)};
        left_page.init();

        while (left_page.free_space() > left_page.total_space() / 2) {
            auto [key, left_child] = take_entry();
            left_page.push_back(SlotExtra::inner(left_child), key);
        }

        auto [next_key, next_left_child] = take_entry();

        mid_key = std::move(next_key);
        left_page.header_extra().last_child = next_left_child;
    }

    {
        NodePage right_page{eng.buf_mgr.fetch_page(fid, right_pnum)};
        right_page.init();

        while (src_slot < og_page.slot_cnt() || mid_pending) {
            auto [key, left_child] = take_entry();
            right_page.push_back(SlotExtra{left_child}, key);
        }

        right_page.header_extra().last_child = og_page.const_header_extra().last_child;
    }

    return {left_pnum, mid_key, right_pnum};
}

std::pair<u32, pnum_t> BTreeIndex::inner_find_child(const NodePage& page, const Value& key) {
    assert(!is_leaf(page.const_header_extra()));
    u32 fit_idx = 0;

    for (; fit_idx < page.slot_cnt(); ++fit_idx) {
        if (key < page.read_data(fit_idx))
            return {fit_idx, page.read_slot_extra(fit_idx).left_child};
    }

    return {fit_idx, page.const_header_extra().last_child};
}

u32 BTreeIndex::leaf_lower_bound_idx(const NodePage& page, const Value& key) {
    assert(is_leaf(page.const_header_extra()));

    u32 ins_idx = 0;

    for (; ins_idx < page.slot_cnt(); ++ins_idx) {
        auto slot_key = page.read_data(ins_idx);

        if (key <= slot_key)
            break;
    }

    return ins_idx;
}

void BTreeIndex::add(const Value& key, Rid rid) {
    auto file_hdr = eng.file_mgr.read_user_header<BTreeHeader>(fid);

    assert(file_hdr.root != PAGE_NIL);  // should exist due to init()

    pnum_t cur_page = file_hdr.root;
    std::stack<std::pair<pnum_t, u32>> path;

    while (true) {
        NodePage page{eng.buf_mgr.fetch_page(fid, cur_page)};
        if (is_leaf(page.const_header_extra()))
            break;

        auto [fit_idx, fit_child] = inner_find_child(page, key);

        path.emplace(cur_page, fit_idx);
        cur_page = fit_child;
    }

    pnum_t leaf_pnum = cur_page;

    u32 ins_idx = 0;

    {
        NodePage leaf_page{eng.buf_mgr.fetch_page(fid, leaf_pnum)};

        ins_idx = leaf_lower_bound_idx(leaf_page, key);

        if (leaf_page.will_fit(key)) {
            // a non-overflowing insert NEVER causes inner node updates. I have
            // discovered a truly marvelous proof of this, which this comment
            // is too narrow to contain.
            leaf_page.insert(ins_idx, SlotExtra::leaf(rid), key);
            return;
        }
    }

    auto [left_pnum, right_pnum] = leaf_insert_split(leaf_pnum, ins_idx, key, rid);
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
                auto extra = page.read_slot_extra(idx);
                extra.left_child = right_pnum;
                page.write_slot_extra(idx, extra);
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
}

void BTreeIndex::ugly_print() const {
    auto file_hdr = eng.file_mgr.read_user_header<BTreeHeader>(fid);

    std::queue<std::pair<pnum_t, std::size_t>> q;
    std::size_t last_depth = 0;

    q.emplace(file_hdr.root, last_depth);

    while (!q.empty()) {
        auto [pnum, depth] = q.front();
        q.pop();

        if (depth != last_depth)
            std::println();

        last_depth = depth;

        NodePage page{eng.buf_mgr.fetch_page(fid, pnum)};

        for (u32 i = 0; i < page.slot_cnt(); ++i) {
            std::print("{} ", page.read_data(i));

            if (!is_leaf(page.const_header_extra()))
                q.emplace(page.read_slot_extra(i).left_child, depth + 1);
        }
        std::print("     ");

        if (!is_leaf(page.const_header_extra()))
            q.emplace(page.const_header_extra().last_child, depth + 1);
    }

    std::println();
}

bool BTreeIndex::remove(const Value& key, const Rid& rid) {
    auto file_hdr = eng.file_mgr.read_user_header<BTreeHeader>(fid);

    assert(file_hdr.root != PAGE_NIL);  // should exist due to init()

    pnum_t cur_page = file_hdr.root;
    std::stack<std::pair<pnum_t, u32>> path;

    while (true) {
        NodePage page{eng.buf_mgr.fetch_page(fid, cur_page)};
        if (is_leaf(page.const_header_extra()))
            break;

        auto [fit_idx, fit_child] = inner_find_child(page, key);

        path.emplace(cur_page, fit_idx);
        cur_page = fit_child;
    }

    pnum_t leaf_pnum = cur_page;
    std::optional<Value> updated_min = std::nullopt;

    {
        NodePage leaf_page{eng.buf_mgr.fetch_page(fid, leaf_pnum)};
        u32 i = 0;

        for (; i < leaf_page.slot_cnt(); ++i) {
            if (key == leaf_page.read_data(i))
                break;
        }

        assert(i <= leaf_page.slot_cnt());

        if (i == leaf_page.slot_cnt())
            return false;

        // we must now find the correct RID
        // we do a linear scan from this point on, then swap and remove
        Rid found_rid = leaf_page.read_slot_extra(i).rid;

        {
            pnum_t cur_pnum = leaf_pnum;
            u32 cur_slot = i;

            NodePage cur_page{eng.buf_mgr.fetch_page(fid, cur_pnum)};

            while (true) {
                if (cur_page.read_slot_extra(cur_slot).rid == rid) {
                    cur_page.write_slot_extra(cur_slot, SlotExtra::leaf(found_rid));
                    break;
                }

                cur_slot += 1;

                if (cur_slot == cur_page.slot_cnt()) {
                    cur_slot = 0;
                    cur_pnum = cur_page.const_header_extra().next_page;

                    if (cur_pnum == PAGE_NIL)
                        return false;

                    cur_page = NodePage{eng.buf_mgr.fetch_page(fid, cur_pnum)};
                }
            }
        }

        auto removed_key = leaf_page.read_data(i);
        leaf_page.remove(i);

        if (leaf_page.slot_cnt() > 0)
            updated_min = leaf_page.read_data(0);
    }

    // first path.pop() is an edge case for leaves.
    if (!path.empty()) {
        auto [par_pnum, par_idx] = path.top();
        path.pop();

        NodePage par_page{eng.buf_mgr.fetch_page(fid, par_pnum)};

        if (updated_min && par_idx > 0) {
            par_page.update_data(par_idx - 1, *updated_min);
            updated_min = std::nullopt;
        }

        pnum_t leaf_pnum = child_pnum(par_page, par_idx);
        std::optional<pnum_t> to_free = std::nullopt;

        {
            NodePage leaf_page{eng.buf_mgr.fetch_page(fid, leaf_pnum)};

            if (leaf_page.free_space() > leaf_page.total_space() / 2) {
                if (!leaf_try_borrow(leaf_page, par_page, par_idx)) {
                    if (par_idx > 0) {
                        to_free = leaf_pnum;
                        leaf_merge_with_next(par_page, par_idx - 1);
                    } else {
                        to_free = child_pnum(par_page, par_idx + 1);
                        leaf_merge_with_next(par_page, par_idx);
                    }
                }
            }
        }

        if (to_free)
            eng.file_mgr.free_page(fid, *to_free);
    }

    while (!path.empty()) {
        auto [par_pnum, par_idx] = path.top();
        path.pop();

        NodePage par_page{eng.buf_mgr.fetch_page(fid, par_pnum)};

        if (updated_min && par_idx > 0) {
            par_page.update_data(par_idx - 1, *updated_min);
            updated_min = std::nullopt;  // this did not update the current subtree's min key
        }

        pnum_t inner_pnum = child_pnum(par_page, par_idx);
        std::optional<pnum_t> to_free = std::nullopt;

        {
            NodePage inner_page{eng.buf_mgr.fetch_page(fid, inner_pnum)};

            if (inner_page.free_space() > inner_page.total_space() / 2) {
                if (!inner_try_borrow(inner_page, par_page, par_idx)) {
                    if (par_idx > 0) {
                        to_free = inner_pnum;
                        inner_merge_with_next(par_page, par_idx - 1);
                    } else {
                        to_free = child_pnum(par_page, par_idx + 1);
                        inner_merge_with_next(par_page, par_idx);
                    }
                }
            }
        }

        if (to_free)
            eng.file_mgr.free_page(fid, *to_free);
    }

    {
        std::optional<pnum_t> to_free = std::nullopt;

        {
            NodePage root_page{eng.buf_mgr.fetch_page(fid, file_hdr.root)};
            auto root_extra = root_page.const_header_extra();

            if (!is_leaf(root_extra) && root_page.slot_cnt() == 0) {
                to_free = file_hdr.root;
                file_hdr.root = root_extra.last_child;
                eng.file_mgr.write_user_header(fid, file_hdr);
            }
        }

        if (to_free)
            eng.file_mgr.free_page(fid, *to_free);
    }

    return true;
}

bool BTreeIndex::leaf_try_borrow(NodePage& leaf_page, NodePage& par_page, u32 par_idx) {
    if (par_idx > 0) {
        // borrow from prev
        pnum_t prev_pnum = child_pnum(par_page, par_idx - 1);
        NodePage prev_page{eng.buf_mgr.fetch_page(fid, prev_pnum)};

        u32 last_slot = prev_page.slot_cnt() - 1;

        if (prev_page.free_space() < prev_page.total_space() / 2) {
            Value moved_key = prev_page.read_data(last_slot);
            Rid moved_rid = prev_page.read_slot_extra(last_slot).rid;

            prev_page.remove(prev_page.slot_cnt() - 1);
            leaf_page.insert(0, SlotExtra::leaf(moved_rid), moved_key);

            par_page.update_data(par_idx - 1, moved_key);
            return true;
        }
    }

    if (par_idx < par_page.slot_cnt()) {
        // borrow from next
        pnum_t next_pnum = child_pnum(par_page, par_idx + 1);
        NodePage next_page{eng.buf_mgr.fetch_page(fid, next_pnum)};

        if (next_page.free_space() < next_page.total_space() / 2) {
            Value moved_key = next_page.read_data(0);
            Rid moved_rid = next_page.read_slot_extra(0).rid;

            next_page.remove(0);
            leaf_page.push_back(SlotExtra::leaf(moved_rid), moved_key);

            Value new_right_min = next_page.read_data(0);
            par_page.update_data(par_idx, new_right_min);

            return true;
        }
    }

    return false;
}

bool BTreeIndex::inner_try_borrow(NodePage& inner_page, NodePage& par_page, u32 par_idx) {
    if (par_idx > 0) {
        pnum_t prev_pnum = child_pnum(par_page, par_idx - 1);
        NodePage prev_page{eng.buf_mgr.fetch_page(fid, prev_pnum)};

        u32 last_slot = prev_page.slot_cnt() - 1;

        if (prev_page.free_space() < prev_page.total_space() / 2) {
            Value lifted_key = prev_page.read_data(last_slot);
            Value lowered_key = par_page.read_data(par_idx - 1);

            pnum_t moved_child = prev_page.const_header_extra().last_child;
            pnum_t prev_last_left_child = prev_page.read_slot_extra(last_slot).left_child;

            prev_page.remove(last_slot);
            prev_page.header_extra().last_child = prev_last_left_child;
            par_page.update_data(par_idx - 1, lifted_key);
            inner_page.insert(0, SlotExtra::inner(moved_child), lowered_key);

            return true;
        }
    }

    if (par_idx < par_page.slot_cnt()) {
        pnum_t next_pnum = child_pnum(par_page, par_idx + 1);
        NodePage next_page{eng.buf_mgr.fetch_page(fid, next_pnum)};

        if (next_page.free_space() < next_page.total_space() / 2) {
            Value lifted_key = next_page.read_data(0);
            Value lowered_key = par_page.read_data(par_idx);

            pnum_t moved_child = next_page.read_slot_extra(0).left_child;
            pnum_t prev_right_child = inner_page.const_header_extra().last_child;

            next_page.remove(0);
            par_page.update_data(par_idx, lifted_key);
            inner_page.push_back(SlotExtra::inner(prev_right_child), lowered_key);
            inner_page.header_extra().last_child = moved_child;

            return true;
        }
    }

    return false;
}

void BTreeIndex::leaf_merge_with_next(NodePage& par_page, u32 par_idx) {
    assert(par_idx < par_page.slot_cnt());

    pnum_t leaf_pnum = child_pnum(par_page, par_idx);
    pnum_t next_pnum = child_pnum(par_page, par_idx + 1);

    {
        NodePage leaf_page{eng.buf_mgr.fetch_page(fid, leaf_pnum)};
        NodePage next_page{eng.buf_mgr.fetch_page(fid, next_pnum)};

        pnum_t next_next_pnum = next_page.const_header_extra().next_page;
        leaf_page.header_extra().next_page = next_next_pnum;

        if (next_next_pnum != PAGE_NIL) {
            NodePage next_next_page{eng.buf_mgr.fetch_page(fid, next_next_pnum)};
            next_next_page.header_extra().prev_page = leaf_pnum;
        }

        for (u32 i = 0; i < next_page.slot_cnt(); ++i) {
            Value key = next_page.read_data(i);
            auto extra = next_page.read_slot_extra(i);

            leaf_page.push_back(extra, key);
        }
    }

    if (par_idx == par_page.slot_cnt() - 1) {
        // removing the last key is special
        par_page.remove(par_idx);
        par_page.header_extra().last_child = leaf_pnum;
    } else {
        Value shifted_key = par_page.read_data(par_idx + 1);
        par_page.remove(par_idx + 1);
        par_page.update_data(par_idx, shifted_key);
    }
}

void BTreeIndex::inner_merge_with_next(NodePage& par_page, u32 par_idx) {
    assert(par_idx < par_page.slot_cnt());

    pnum_t inner_pnum = child_pnum(par_page, par_idx);
    pnum_t next_pnum = child_pnum(par_page, par_idx + 1);

    {
        NodePage inner_page{eng.buf_mgr.fetch_page(fid, inner_pnum)};
        NodePage next_page{eng.buf_mgr.fetch_page(fid, next_pnum)};

        pnum_t inner_last_child = inner_page.const_header_extra().last_child;

        Value lowered_key = par_page.read_data(par_idx);
        inner_page.push_back(SlotExtra::inner(inner_last_child), lowered_key);

        for (u32 i = 0; i < next_page.slot_cnt(); ++i) {
            Value key = next_page.read_data(i);
            auto extra = next_page.read_slot_extra(i);

            inner_page.push_back(extra, key);
        }

        inner_page.header_extra().last_child = next_page.const_header_extra().last_child;
    }

    if (par_idx == par_page.slot_cnt() - 1) {
        // removing the last key is special
        par_page.remove(par_idx);
        par_page.header_extra().last_child = inner_pnum;
    } else {
        Value shifted_key = par_page.read_data(par_idx + 1);
        par_page.remove(par_idx + 1);
        par_page.update_data(par_idx, shifted_key);
    }
}

[[nodiscard]] pnum_t BTreeIndex::child_pnum(const NodePage& page, u32 child_idx) {
    if (child_idx == page.slot_cnt())
        return page.const_header_extra().last_child;

    return page.read_slot_extra(child_idx).left_child;
}

using RangeCursor = BTreeIndex::RangeCursor;

RangeCursor::RangeCursor(BufferManager& buf_mgr,
                         FileId fid,
                         pnum_t init_page,
                         u32 init_slot,
                         Value key_high)
    : fid{fid},
      buf_mgr{buf_mgr},
      cur_pnum{init_page},
      cur_slot{init_slot},
      key_high{std::move(key_high)} {}

RangeCursor::RangeCursor(BufferManager& buf_mgr)
    : buf_mgr{buf_mgr},
      cur_pnum{0},
      cur_slot{0},
      finished{true} {}

std::optional<Rid> RangeCursor::next() {
    if (cur_pnum == PAGE_NIL) {
        assert(!page);
        return std::nullopt;
    }

    if (!page)
        page = NodePage{buf_mgr.fetch_page(fid, cur_pnum)};

    assert(cur_slot <= page->slot_cnt());

    if (cur_slot == page->slot_cnt()) {
        cur_slot = 0;
        cur_pnum = page->const_header_extra().next_page;
        page.reset();
        return next();
    }

    Value key = page->read_data(cur_slot);

    if (key > key_high) {
        cur_slot = 0;
        cur_pnum = PAGE_NIL;
        page.reset();
        return next();
    }

    Rid rid = page->read_slot_extra(cur_slot).rid;
    cur_slot += 1;

    return rid;
}

RangeCursor BTreeIndex::range_search(const Value& key_low, const Value& key_high) {
    auto file_hdr = eng.file_mgr.read_user_header<BTreeHeader>(fid);
    assert(file_hdr.root != PAGE_NIL);  // should exist due to init()

    pnum_t cur_page = file_hdr.root;

    while (true) {
        NodePage page{eng.buf_mgr.fetch_page(fid, cur_page)};
        if (is_leaf(page.const_header_extra()))
            break;

        auto [_, fit_child] = inner_find_child(page, key_low);
        cur_page = fit_child;
    }

    u32 start_slot = 0;

    {
        NodePage leaf_page{eng.buf_mgr.fetch_page(fid, cur_page)};

        for (; start_slot < leaf_page.slot_cnt(); ++start_slot) {
            if (leaf_page.read_data(start_slot) >= key_low)
                break;
        }
    }

    return RangeCursor{eng.buf_mgr, fid, cur_page, start_slot, key_high};
}
