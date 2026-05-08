#ifndef RTREE_HPP
#define RTREE_HPP

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <queue>
#include <stdexcept>
#include <vector>
#include "rect.hpp"

template<typename Coord, std::size_t N, typename T, std::size_t MAX>
class RTree {
public:
    struct Node;
    struct Branch;

    using NodePtr = std::unique_ptr<Node>;
    using VariantType = std::variant<T, NodePtr>;

    enum class Group : std::int8_t {
        Unassigned = -1,
        A = 0,
        B = 1
    };

    struct Branch {
        Rect<Coord, N> rect{};
        VariantType data{};

        [[nodiscard]] bool is_leaf() const {
            return std::holds_alternative<T>(data);
        }

        [[nodiscard]] bool is_internal() const {
            return std::holds_alternative<NodePtr>(data);
        }

        [[nodiscard]] T& value() {
            return std::get<T>(data);
        }

        [[nodiscard]] const T& value() const {
            return std::get<T>(data);
        }

        [[nodiscard]] NodePtr& child() {
            return std::get<NodePtr>(data);
        }

        [[nodiscard]] const NodePtr& child() const {
            return std::get<NodePtr>(data);
        }
    };

    struct Node {
        std::array<Branch, MAX> branches{};
        std::size_t count{0};
        std::size_t level{0};
    };

    struct Partition {
        std::array<Branch, MAX + 1> buffer;
        std::array<Group, MAX + 1> group;
        std::size_t total = 0;
        std::size_t min_fill = MAX / 2;
        std::array<Rect<Coord, N>, 2> cover;
        std::array<Coord, 2> area{};
        std::array<std::size_t, 2> count{};
    };

    struct KNNEntry {
        const Branch* branch;
        Coord distance;
    };

private:
    NodePtr root;

    Rect<Coord, N> node_cover(const Node* node) {
        if (node->count == 0) {
            throw std::runtime_error("Empty node has no MBR");
        }
        Rect result = node->branches[0].rect;
        for (std::size_t i = 1; i < node->count; ++i) {
            result = result.merge(node->branches[i].rect);
        }
        return result;
    }

    void add_branch_no_split(Node* node, Branch&& branch) {
        if (node->count == MAX) {
            throw std::runtime_error("Node overflow");
        }
        node->branches[node->count++] = std::move(branch);
    }

    bool add_branch(Node* node, Branch&& branch, NodePtr& new_node) {
        if (node->count < MAX) {
            add_branch_no_split(node, std::move(branch));
            return false;
        }
        split_node(node, std::move(branch), new_node);
        return true;
    }

    void remove_at(Node* node, std::size_t idx) {
        if (idx >= node->count) {
            throw std::out_of_range("remove_at out of range");
        }
        node->branches[idx] = std::move(node->branches[node->count - 1]);
        --node->count;
    }

    void search_recursive(const Node* node,
                          const Rect<Coord, N>& rect,
                          std::vector<T>& results) const {
        for (std::size_t i = 0; i < node->count; ++i) {
            const auto& branch = node->branches[i];
            if (!branch.rect.intersects(rect)) {
                continue;
            }
            if (branch.is_leaf()) {
                results.push_back(branch.value());
            } else {
                search_recursive(branch.child().get(), rect, results);
            }
        }
    }

    void split_node(Node* node, Branch new_branch, NodePtr& out_new_node) {
        Partition partition;

        for (std::size_t i = 0; i < node->count; ++i) {
            partition.buffer[i] = std::move(node->branches.at(i));
        }
        partition.buffer[node->count] = std::move(new_branch);
        partition.total = node->count + 1;
        partition.group.fill(Group::Unassigned);

        pick_seeds(partition);
        choose_partition(partition);

        out_new_node = std::make_unique<Node>();
        out_new_node->level = node->level;
        node->count = 0;

        for (std::size_t i = 0; i < partition.total; ++i) {
            if (partition.group[i] == Group::A) {
                add_branch_no_split(node, std::move(partition.buffer[i]));
            } else {
                add_branch_no_split(out_new_node.get(), std::move(partition.buffer[i]));
            }
        }
    }

    void pick_seeds(Partition& partition) {
        Coord worst = std::numeric_limits<Coord>::lowest();
        std::size_t seed1 = 0;
        std::size_t seed2 = 1;

        for (std::size_t i = 0; i < partition.total - 1; ++i) {
            for (std::size_t j = i + 1; j < partition.total; ++j) {
                const auto combined = partition.buffer[i].rect.merge(partition.buffer[j].rect);
                const Coord waste = combined.volume() - partition.buffer[i].rect.volume() -
                                    partition.buffer[j].rect.volume();

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

    void assign(Partition& partition, std::size_t idx, Group group) {
        partition.group[idx] = group;
        auto group_idx = static_cast<std::size_t>(group);
        partition.cover[group_idx] =
            (partition.count[group_idx] == 0)
                ? partition.buffer[idx].rect
                : partition.cover[group_idx].merge(partition.buffer[idx].rect);

        partition.area[group_idx] = partition.cover[group_idx].volume();
        partition.count[group_idx]++;
    }

    void choose_partition(Partition& partition) {
        auto assign_remaining = [&](Group group) {
            for (std::size_t i = 0; i < partition.total; ++i) {
                if (partition.group[i] == Group::Unassigned) {
                    assign(partition, i, group);
                }
            }
        };

        auto pick_next = [&]() -> std::pair<std::size_t, Group> {
            Coord best_diff = std::numeric_limits<Coord>::max();
            std::size_t best_idx = 0;
            Group best_group = Group::Unassigned;

            for (std::size_t i = 0; i < partition.total; ++i) {
                if (partition.group[i] != Group::Unassigned) {
                    continue;
                }

                const auto& rect = partition.buffer[i].rect;

                auto merged0 = partition.cover[0].merge(rect);
                auto merged1 = partition.cover[1].merge(rect);

                Coord cost0 = merged0.volume() - partition.area[0];
                Coord cost1 = merged1.volume() - partition.area[1];

                Coord diff = std::abs(cost0 - cost1);
                Group preferred = (cost0 < cost1) ? Group::A : Group::B;

                if (diff < best_diff) {
                    best_diff = diff;
                    best_idx = i;
                    best_group = preferred;
                }
            }

            return {best_idx, best_group};
        };

        while (partition.count[0] + partition.count[1] < partition.total) {
            if (partition.count[0] >= partition.total - partition.min_fill) {
                assign_remaining(Group::B);
                break;
            }

            if (partition.count[1] >= partition.total - partition.min_fill) {
                assign_remaining(Group::A);
                break;
            }

            auto [idx, group] = pick_next();
            assign(partition, idx, group);
        }
    }

    std::size_t choose_subtree(const Node* node, const Rect<Coord, N>& rect) const {
        if (node->level == 0) {
            throw std::runtime_error("choose_subtree called on leaf node");
        }

        if (node->count == 0) {
            throw std::runtime_error("Internal node has no branches");
        }

        std::size_t best_index = 0;
        Coord best_enlargement = std::numeric_limits<Coord>::max();
        Coord best_volume = std::numeric_limits<Coord>::max();

        for (std::size_t i = 0; i < node->count; ++i) {
            const Rect<Coord, N>& current_rect = node->branches[i].rect;
            const Coord current_volume = current_rect.volume();
            const Coord current_enlargement = current_rect.merge(rect).volume() - current_volume;

            if (current_enlargement < best_enlargement ||
                (current_enlargement == best_enlargement && current_volume < best_volume)) {
                best_index = i;
                best_enlargement = current_enlargement;
                best_volume = current_volume;
            }
        }
        return best_index;
    }

    bool insert_recursive(Node* node,
                          Branch&& branch,
                          NodePtr& new_node,
                          std::size_t target_level) {
        if (node->level > target_level) {
            std::size_t idx = choose_subtree(node, branch.rect);
            auto* child = node->branches.at(idx).child().get();

            NodePtr split_node_ptr;
            Rect original_rect = branch.rect;
            bool child_split =
                insert_recursive(child, std::move(branch), split_node_ptr, target_level);

            if (!child_split) {
                node->branches.at(idx).rect = node->branches.at(idx).rect.merge(original_rect);
                return false;
            }

            node->branches.at(idx).rect = node_cover(child);

            auto new_branch_cover = node_cover(split_node_ptr.get());
            Branch new_branch{new_branch_cover, std::move(split_node_ptr)};

            return add_branch(node, std::move(new_branch), new_node);
        }

        if (node->level == target_level) {
            return add_branch(node, std::move(branch), new_node);
        }

        throw std::runtime_error("Invalid tree level in insert");
    }

    void reinsert(NodePtr&& node, std::vector<NodePtr>& list) {
        list.push_back(std::move(node));
    }

    bool remove_recursive(const Rect<Coord, N>* rect,
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

    bool remove(const Rect<Coord, N>* rect, const T& value) {
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

    Coord rect_distance_sq(const Rect<Coord, N>& a, const Rect<Coord, N>& b) const {
        return a.min_distance_sq(b);
    }

    std::vector<std::pair<T, Coord>> knn(const Rect<Coord, N>& query, std::size_t k) const {
        std::vector<std::pair<T, Coord>> result;

        if (!root || k == 0) {
            return result;
        }

        auto cmp = [](const KNNEntry& a, const KNNEntry& b) { return a.distance > b.distance; };
        std::priority_queue<KNNEntry, std::vector<KNNEntry>, decltype(cmp)> pq(cmp);

        for (std::size_t i = 0; i < root->count; ++i) {
            const auto& branch = root->branches.at(i);
            Coord dist = rect_distance_sq(query, branch.rect);
            pq.push(KNNEntry{&branch, dist});
        }

        while (!pq.empty() && result.size() < k) {
            auto current = pq.top();
            pq.pop();

            const Branch* branch = current.branch;

            if (branch->is_internal()) {
                const auto* node = branch->child().get();

                for (std::size_t i = 0; i < node->count; ++i) {
                    const auto& child_branch = node->branches.at(i);
                    Coord dist = rect_distance_sq(query, child_branch.rect);

                    pq.push(KNNEntry{&child_branch, dist});
                }
            } else {
                result.emplace_back(branch->value(), current.distance);
            }
        }

        return result;
    }

public:
    RTree() = default;

    std::vector<T> search(const Rect<Coord, N>& rect) const {
        std::vector<T> results;
        if (root) {
            search_recursive(root.get(), rect, results);
        }
        return results;
    }

    void insert(const Rect<Coord, N>& rect, T value) {
        if (!root) {
            root = std::make_unique<Node>();
            root->level = 0;
        }

        Branch branch{rect, std::move(value)};
        NodePtr new_node;

        bool root_split = insert_recursive(root.get(), std::move(branch), new_node, 0);

        if (root_split) {
            auto new_root = std::make_unique<Node>();
            new_root->level = root->level + 1;

            auto old_cover = node_cover(root.get());
            add_branch_no_split(new_root.get(), Branch{old_cover, std::move(root)});

            auto new_cover = node_cover(new_node.get());
            add_branch_no_split(new_root.get(), Branch{new_cover, std::move(new_node)});

            root = std::move(new_root);
        }
    }

    bool remove(const T& value) {
        return remove(nullptr, value);
    }

    bool remove(const Rect<Coord, N>& rect, const T& value) {
        return remove(&rect, value);
    }

    std::vector<T> knn_search(const Rect<Coord, N>& query, std::size_t k) const {
        auto pairs = knn(query, k);

        std::vector<T> result;
        result.reserve(pairs.size());

        for (auto& [value, _] : pairs) {
            result.push_back(value);
        }

        return result;
    }
};

#endif
