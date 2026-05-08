#ifndef RTREE_HPP
#define RTREE_HPP

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <print>
#include <queue>
#include <ranges>
#include <stack>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>
#include "engine/buffer_manager.hpp"
#include "engine/engine.hpp"
#include "engine/file_manager.hpp"
#include "layout/slotted_page.hpp"
#include "seq_file.hpp"
#include "types.hpp"
#include "util.hpp"

template<std::size_t N>
using Point = std::array<f64, N>;

template<std::size_t N>
struct Rect {
    Point<N> min{};
    Point<N> max{};

    Rect() = default;

    Rect(Point<N> min, Point<N> max)
        : min{std::move(min)},
          max{std::move(max)} {}

    [[nodiscard]] f64 volume() const {
        f64 vol = 1;

        for (std::size_t i = 0; i < N; ++i)
            vol *= max[i] - min[i];

        return vol;
    }

    [[nodiscard]] Rect merge(const Rect& other) const {
        Rect result{};

        for (std::size_t i = 0; i < N; ++i) {
            result.min[i] = std::min(min[i], other.min[i]);
            result.max[i] = std::max(max[i], other.max[i]);
        }

        return result;
    }

    [[nodiscard]] bool intersects(const Rect& other) const {
        for (std::size_t i = 0; i < N; ++i) {
            if (max[i] < other.min[i] || other.max[i] < min[i])
                return false;
        }

        return true;
    }

    [[nodiscard]] f64 min_distance_sq(const Rect& other) const {
        f64 dist = 0;

        for (std::size_t i = 0; i < N; ++i) {
            f64 diff1 = min[i] - other.max[i];
            f64 diff2 = max[i] - other.min[i];

            if (diff1 * diff2 < 0)
                continue;

            f64 d1 = diff1 * diff1;
            f64 d2 = diff2 * diff2;

            dist += std::min(d1, d2);
        }

        return dist;
    }
};

template<std::size_t N>
class RTreeIndex {
private:
    Engine& eng;
    FileId fid;

    struct RTreeHeader {
        pnum_t root = PAGE_NIL;
    };

    struct NodeHeader {
        u64 level = 0;
    };

    struct SlotExtra {
        Rect<N> rect{};
    };

    using SlotValue = std::variant<Rid, pnum_t>;
    using NodePage = SlottedPage<NodeHeader, SlotExtra, SlotValue>;

    u32 choose_subtree(const NodePage& page, const Rect<N>& rect) const {
        if (page.const_header_extra().level == 0)
            throw std::invalid_argument("choose_subtree called on leaf node");

        if (page.slot_cnt() == 0)
            throw std::invalid_argument("internal node has no branches");

        u32 best = 0;
        f64 best_enlargement = std::numeric_limits<f64>::max();
        f64 best_volume = std::numeric_limits<f64>::max();

        for (u32 i = 0; i < page.slot_cnt(); ++i) {
            auto cur_rect = page.read_slot_extra(i).rect;
            const f64 cur_volume = cur_rect.volume();
            const f64 cur_enlargement = cur_rect.merge(rect).volume() - cur_volume;

            // NOTE: won't cur_enlargement == best_enlargement always be false
            // due to float precision?
            if (cur_enlargement < best_enlargement ||
                (cur_enlargement == best_enlargement && cur_volume < best_volume)) {
                best = i;
                best_enlargement = cur_enlargement;
                best_volume = cur_volume;
            }
        }

        return best;
    }

    enum class Group : std::int8_t {
        Unassigned = -1,
        A = 0,
        B = 1
    };

    struct Partition {
        std::vector<std::pair<SlotExtra, SlotValue>> buffer;
        std::vector<Group> group;
        std::array<Rect<N>, 2> cover;
        std::array<f64, 2> area{};
        std::array<std::size_t, 2> count{};
    };

    void pick_seeds(Partition& partition) {
        f64 worst = std::numeric_limits<f64>::lowest();
        u32 seed1 = 0;
        u32 seed2 = 1;

        u32 total = partition.buffer.size();

        for (u32 i = 0; i < total - 1; ++i) {
            for (u32 j = i + 1; j < total; ++j) {
                const auto combined =
                    partition.buffer[i].first.rect.merge(partition.buffer[j].first.rect);
                const f64 waste = combined.volume() - partition.buffer[i].first.rect.volume() -
                                  partition.buffer[j].first.rect.volume();

                if (waste > worst) {
                    worst = waste;
                    seed1 = i;
                    seed2 = j;
                }
            }
        }

        assign(partition, seed1, Group::A);
        assign(partition, seed2, Group::B);
    }

    void assign(Partition& partition, u32 idx, Group group) {
        partition.group[idx] = group;

        auto group_idx = static_cast<std::size_t>(group);
        partition.cover[group_idx] =
            (partition.count[group_idx] == 0)
                ? partition.buffer[idx].first.rect
                : partition.cover[group_idx].merge(partition.buffer[idx].first.rect);

        partition.area[group_idx] = partition.cover[group_idx].volume();
        partition.count[group_idx]++;
    }

    void choose_partition(Partition& partition) {
        u32 total = partition.buffer.size();

        auto assign_remaining = [&](Group group) {
            for (auto [i, prev_group] : std::views::enumerate(partition.group)) {
                if (prev_group == Group::Unassigned)
                    assign(partition, i, group);
            }
        };

        auto pick_next = [&]() {
            f64 best_diff = std::numeric_limits<f64>::max();
            u32 best_idx = 0;
            Group best_group = Group::Unassigned;

            for (u32 i = 0; i < total; ++i) {
                if (partition.group[i] != Group::Unassigned)
                    continue;

                const Rect<N>& rect = partition.buffer[i].first.rect;

                auto merged0 = partition.cover[0].merge(rect);
                auto merged1 = partition.cover[1].merge(rect);

                f64 cost0 = merged0.volume() - partition.area[0];
                f64 cost1 = merged1.volume() - partition.area[1];

                f64 diff = std::abs(cost0 - cost1);
                Group preferred = (cost0 < cost1) ? Group::A : Group::B;

                if (diff < best_diff) {
                    best_diff = diff;
                    best_idx = i;
                    best_group = preferred;
                }
            }

            return std::make_pair(best_idx, best_group);
        };

        while (partition.count[0] + partition.count[1] < total) {
            if (partition.count[0] >= total / 2) {
                assign_remaining(Group::B);
                break;
            }

            if (partition.count[1] >= total / 2) {
                assign_remaining(Group::A);
                break;
            }

            auto [idx, group] = pick_next();
            assign(partition, idx, group);
        }
    }

    void split_node(NodePage& page, Rect<N> ins_rect, SlotValue ins_val, pnum_t& out_new_pnum) {
        Partition partition;

        for (u32 i = 0; i < page.slot_cnt(); ++i)
            partition.buffer.emplace_back(page.read_slot_extra(i), page.read_data(i));

        partition.buffer.emplace_back(SlotExtra{std::move(ins_rect)}, ins_val);
        partition.group = std::vector<Group>(partition.buffer.size(), Group::Unassigned);

        pick_seeds(partition);
        choose_partition(partition);

        out_new_pnum = eng.file_mgr.alloc_page(fid);

        NodePage new_page{eng.buf_mgr.fetch_page(fid, out_new_pnum)};
        new_page.init();
        new_page.header_extra().level = page.const_header_extra().level;

        page.clear();

        for (auto [i, data] : std::views::enumerate(partition.buffer)) {
            auto [rect, val] = std::move(data);

            if (partition.group[i] == Group::A)
                page.push_back(SlotExtra{std::move(rect)}, val);
            else
                new_page.push_back(SlotExtra{std::move(rect)}, val);
        }
    }

    bool add_branch(NodePage& page, const Rect<N>& ins_rect, SlotValue val, pnum_t& new_pnum) {
        if (page.will_fit(val)) {
            page.push_back(SlotExtra{std::move(ins_rect)}, val);
            return false;
        }

        split_node(page, std::move(ins_rect), val, new_pnum);
        return true;
    }

    Rect<N> node_cover(const NodePage& page) {
        if (page.slot_cnt() == 0) {
            throw std::runtime_error("Empty node has no MBR");
        }

        Rect result = page.read_slot_extra(0).rect;

        for (std::size_t i = 1; i < page.slot_cnt(); ++i)
            result = result.merge(page.read_slot_extra(i).rect);

        return result;
    }

    bool insert_recursive(pnum_t pnum,
                          const Rect<N>& ins_rect,
                          SlotValue ins_val,
                          pnum_t& new_pnum,
                          std::size_t target_level) {
        u64 level = 0;
        u32 idx = 0;
        pnum_t child_pnum = PAGE_NIL;

        {
            NodePage page{eng.buf_mgr.fetch_page(fid, pnum)};
            level = page.const_header_extra().level;

            if (level < target_level)
                throw std::invalid_argument("invalid rtree level in insert");

            if (level == target_level)
                return add_branch(page, std::move(ins_rect), ins_val, new_pnum);

            idx = choose_subtree(page, ins_rect);
            child_pnum = std::get<pnum_t>(page.read_data(idx));
        }

        pnum_t split_pnum = PAGE_NIL;
        bool child_split =
            insert_recursive(child_pnum, ins_rect, ins_val, split_pnum, target_level);

        NodePage page{eng.buf_mgr.fetch_page(fid, pnum)};

        if (!child_split) {
            Rect prev_rect = page.read_slot_extra(idx).rect;
            page.write_slot_extra(idx, SlotExtra{prev_rect.merge(ins_rect)});
            return false;
        }

        Rect<N> new_child_cover{};
        {
            NodePage child_page{eng.buf_mgr.fetch_page(fid, child_pnum)};
            new_child_cover = node_cover(child_page);
        }

        page.write_slot_extra(idx, SlotExtra{new_child_cover});

        Rect<N> new_branch_cover{};
        {
            NodePage split_page{eng.buf_mgr.fetch_page(fid, split_pnum)};
            new_branch_cover = node_cover(split_page);
        }

        return add_branch(page, new_branch_cover, split_pnum, new_pnum);
    }

public:
    class Cursor {
    public:
        using value_type = Rid;

        explicit Cursor(FileId fid, BufferManager& buf_mgr, Rect<N> rect, pnum_t root)
            : fid{fid},
              buf_mgr{buf_mgr},
              rect{std::move(rect)} {
            q.push(root);
        }

        std::optional<value_type> next() {
            while (true) {
                if (!page) {
                    if (q.empty())
                        return std::nullopt;

                    pnum_t pnum = q.front();
                    q.pop();

                    page = NodePage{buf_mgr.fetch_page(fid, pnum)};
                    cur_slot = 0;
                }

                assert(!page || cur_slot <= page->slot_cnt());

                if (page && cur_slot == page->slot_cnt()) {
                    page.reset();
                    cur_slot = 0;
                    continue;
                }

                auto extra = page->read_slot_extra(cur_slot);
                auto data = page->read_data(cur_slot);
                cur_slot += 1;

                if (!rect.intersects(extra.rect))
                    continue;

                if (auto* rid = std::get_if<Rid>(&data))
                    return *rid;

                q.push(std::get<pnum_t>(data));
            }
        }

    private:
        FileId fid;
        BufferManager& buf_mgr;
        std::optional<NodePage> page;
        u32 cur_slot = 0;
        std::queue<pnum_t> q;
        Rect<N> rect;
    };

    static_assert(util::iter_of<Cursor, Rid>);

    class KnnCursor {
    public:
        using value_type = Rid;

        KnnCursor(FileId fid, BufferManager& buf_mgr, Rect<N> query, u64 k, pnum_t root_pnum)
            : fid{fid},
              buf_mgr{buf_mgr},
              query{std::move(query)},
              k{k} {
            NodePage root_page{buf_mgr.fetch_page(fid, root_pnum)};

            for (u32 i = 0; i < root_page.slot_cnt(); ++i) {
                Rect<N> rect = root_page.read_slot_extra(i).rect;
                SlotValue val = root_page.read_data(i);
                f64 dist = query.min_distance_sq(rect);
                pq.emplace(std::move(rect), std::move(val), dist);
            }
        }

        std::optional<value_type> next() {
            while (!pq.empty() && k > 0) {
                auto entry = pq.top();
                pq.pop();

                if (const auto* rid = std::get_if<Rid>(&entry.val)) {
                    k -= 1;
                    return *rid;
                }

                auto child_pnum = std::get<pnum_t>(entry.val);
                NodePage child_page{buf_mgr.fetch_page(fid, child_pnum)};

                for (u32 i = 0; i < child_page.slot_cnt(); ++i) {
                    Rect<N> rect = child_page.read_slot_extra(i).rect;
                    SlotValue val = child_page.read_data(i);
                    f64 dist = query.min_distance_sq(rect);

                    pq.emplace(std::move(rect), std::move(val), dist);
                }
            }

            pq = {};
            return std::nullopt;
        }

    private:
        struct KnnEntry {
            Rect<N> rect;
            SlotValue val;
            f64 distance = 0;

            bool operator>(const KnnEntry& other) const {
                return distance > other.distance;
            }
        };

        FileId fid;
        BufferManager& buf_mgr;
        std::priority_queue<KnnEntry, std::vector<KnnEntry>, std::greater<KnnEntry>> pq;
        Rect<N> query;
        u64 k;
    };

    static_assert(util::iter_of<KnnCursor, Rid>);

    RTreeIndex(Engine& engine, FileId fid)
        : eng{engine},
          fid{fid} {}

    void ugly_print() const {
        auto file_hdr = eng.file_mgr.read_user_header<RTreeHeader>(fid);

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
            auto extra = page.const_header_extra();

            std::print("({}): ", extra.level);

            for (u32 i = 0; i < page.slot_cnt(); ++i) {
                auto extra = page.read_slot_extra(i);
                std::print("{}-{} ", extra.rect.min, extra.rect.max);

                auto data = page.read_data(i);
                if (const auto* child_pnum = std::get_if<pnum_t>(&data))
                    q.emplace(*child_pnum, depth + 1);
            }

            std::print("      ");
        }

        std::println();
    }

    void init() {
        eng.file_mgr.init_file(fid);

        // init header
        RTreeHeader file_hdr{};
        file_hdr.root = eng.file_mgr.alloc_page(fid);
        eng.file_mgr.write_user_header(fid, file_hdr);

        NodePage root_page{eng.buf_mgr.fetch_page(fid, file_hdr.root)};
        root_page.init();
    }

    void add(const Point<N>& key, Rid rid) {
        auto file_hdr = eng.file_mgr.read_user_header<RTreeHeader>(fid);
        std::stack<pnum_t> path;

        pnum_t new_pnum = PAGE_NIL;
        Rect ins_rect{key, key};

        bool root_split = insert_recursive(file_hdr.root, ins_rect, rid, new_pnum, 0);

        if (root_split) {
            assert(new_pnum != PAGE_NIL);
            pnum_t new_root_pnum = eng.file_mgr.alloc_page(fid);

            {
                NodePage new_root_page{eng.buf_mgr.fetch_page(fid, new_root_pnum)};
                new_root_page.init();

                {
                    NodePage root_page{eng.buf_mgr.fetch_page(fid, file_hdr.root)};
                    new_root_page.header_extra().level = 1 + root_page.const_header_extra().level;

                    Rect old_cover = node_cover(root_page);
                    new_root_page.push_back(SlotExtra{old_cover}, file_hdr.root);
                }

                {
                    NodePage new_page{eng.buf_mgr.fetch_page(fid, new_pnum)};
                    Rect new_cover = node_cover(new_page);
                    new_root_page.push_back(SlotExtra{new_cover}, new_pnum);
                }
            }

            file_hdr.root = new_root_pnum;
            eng.file_mgr.write_user_header(fid, file_hdr);
        }
    }

    bool remove_recursive(const Rect<N>& rect, pnum_t pnum, std::vector<pnum_t>& reinsert_list) {
        NodePage page{eng.buf_mgr.fetch_page(fid, pnum)};
        if (page.const_header_extra().level == 0) {
            for (u32 i = 0; i < page.slot_cnt(); ++i) {
            }
        }
    }

    bool remove(const Rect<N>& rect) {
        auto file_hdr = eng.file_mgr.read_user_header<RTreeHeader>(fid);
        std::vector<pnum_t> reinsert_list;

        if (!remove_recursive(rect, file_hdr.root, reinsert_list))
            return false;

        for (auto& pnum : reinsert_list) {
            NodePage page{eng.buf_mgr.fetch_page(fid, pnum)};

            for (u32 i = 0; i < page.slot_cnt(); ++i) {
                pnum_t new_pnum = PAGE_NIL;

                auto slot_extra = page.read_slot_extra(i);
                auto header_extra = page.const_header_extra();
                auto data = page.read_data(i);

                insert_recursive(file_hdr.root, slot_extra.rect, std::move(data), new_pnum,
                                 header_extra.level);
            }
        }

        NodePage root_page{eng.buf_mgr.fetch_page(fid, file_hdr.root)};

        if (root_page.slot_cnt() == 1 && root_page.const_header_extra().level != 0) {
            file_hdr.root = std::get<pnum_t>(root_page.read_data(0));
            eng.file_mgr.write_user_header(fid, file_hdr);
        }
    }

    [[nodiscard]] Cursor search(Rect<N> rect) {
        auto file_hdr = eng.file_mgr.read_user_header<RTreeHeader>(fid);
        return Cursor{fid, eng.buf_mgr, std::move(rect), file_hdr.root};
    }

    void range_search(const Point<N>& center, f64 radius);

    [[nodiscard]] KnnCursor knn(const Point<N>& center, u64 k) {
        Rect<N> query{center, center};
        auto file_hdr = eng.file_mgr.read_user_header<RTreeHeader>(fid);
        return KnnCursor{fid, eng.buf_mgr, std::move(query), k, file_hdr.root};
    }
};

template<std::size_t N, typename T, std::size_t MAX>
class RTree {
public:
    struct Node;
    struct Branch;

    using NodePtr = std::unique_ptr<Node>;
    using VariantType = std::variant<T, NodePtr>;

    struct KnnEntry {
        const Branch* branch;
        f64 distance;
    };

private:
    NodePtr root;

    void remove_at(Node* node, std::size_t idx) {
        if (idx >= node->count) {
            throw std::out_of_range("remove_at out of range");
        }
        node->branches[idx] = std::move(node->branches[node->count - 1]);
        --node->count;
    }

    bool remove_recursive(const Rect<N>* rect,
                          const T& value,
                          Node* node,
                          std::vector<NodePtr>& reinsert_list) {
        if (node->level > 0) {
            for (std::size_t i = 0; i < node->count; ++i) {
                auto& branch = node->branches.at(i);

                if ((rect == nullptr) || branch.rect.intersects(*rect)) {
                    if (!remove_recursive(rect, value, branch.child().get(), reinsert_list)) {
                        auto& child_ptr = branch.child();

                        if (child_ptr->count >= MAX / 2) {
                            branch.rect = node_cover(child_ptr.get());
                        } else {
                            reinsert(std::move(child_ptr), reinsert_list);
                            remove_at(node, i);
                        }
                        return false;
                    }
                }
            }
            return true;
        }
        for (std::size_t i = 0; i < node->count; ++i) {
            auto& branch = node->branches.at(i);

            if (branch.is_leaf() && branch.value() == value) {
                remove_at(node, i);
                return false;
            }
        }
        return true;
    }

    bool remove(const Rect<N>* rect, const T& value) {
        if (!root) {
            return false;
        }

        std::vector<NodePtr> reinsert_list;

        bool not_found = remove_recursive(rect, value, root.get(), reinsert_list);

        if (!not_found) {
            for (auto& node : reinsert_list) {
                for (std::size_t i = 0; i < node->count; ++i) {
                    NodePtr new_node;
                    insert_recursive(root.get(), std::move(node->branches.at(i)), new_node,
                                     node->level);
                }
            }

            if (root->count == 1 && root->level != 0) {
                root = std::move(root->branches.at(0).child());
            }

            return true;
        }

        return false;
    }

    std::vector<T> radius_search(const std::array<Coord, N>& point, Coord radius) const {
        std::vector<T> results;

        if (!root) {
            return results;
        }

        Rect<Coord, N> search_rect;

        for (std::size_t i = 0; i < N; ++i) {
            search_rect.min[i] = point[i] - radius;
            search_rect.max[i] = point[i] + radius;
        }

        radius_search_recursive(root.get(), point, radius * radius, search_rect, results);

        return results;
    }

    void radius_search_recursive(const Node* node,
                                 const std::array<Coord, N>& point,
                                 Coord radius_sq,
                                 const Rect<Coord, N>& search_rect,
                                 std::vector<T>& results) const {
        for (std::size_t i = 0; i < node->count; ++i) {
            const auto& branch = node->branches[i];

            if (!branch.rect.intersects(search_rect)) {
                continue;
            }

            if (std::holds_alternative<T>(branch.data)) {
                Coord dist_sq = 0;

                for (std::size_t d = 0; d < N; ++d) {
                    Coord v = point[d];

                    if (v < branch.rect.min[d]) {
                        Coord diff = branch.rect.min[d] - v;
                        dist_sq += diff * diff;
                    } else if (v > branch.rect.max[d]) {
                        Coord diff = v - branch.rect.max[d];
                        dist_sq += diff * diff;
                    }
                }

                if (dist_sq <= radius_sq) {
                    results.push_back(branch.value());
                }

            } else {
                radius_search_recursive(branch.child().get(), point, radius_sq, search_rect,
                                        results);
            }
        }
    }

public:
    RTree() = default;

    bool remove(const T& value) {
        return remove(nullptr, value);
    }

    bool remove(const Rect<N>& rect, const T& value) {
        return remove(&rect, value);
    }
};

#endif
