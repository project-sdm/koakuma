#ifndef BTREE_HPP
#define BTREE_HPP

#include <optional>
#include <tuple>
#include <utility>
#include "engine/buffer_manager.hpp"
#include "engine/engine.hpp"
#include "engine/file_manager.hpp"
#include "layout/slotted_page.hpp"
#include "seq_file.hpp"

// (actually a b+ tree)
class BTreeIndex {
private:
    struct BTreeHeader {
        pnum_t root = PAGE_NIL;
    };

    struct NodeExtra {
        pnum_t last_child = PAGE_NIL;
        pnum_t next_page = PAGE_NIL;
        pnum_t prev_page = PAGE_NIL;
    };

    union SlotExtra {
        pnum_t left_child;
        Rid rid;

        static SlotExtra inner(pnum_t left_child) {
            return {.left_child = left_child};
        }

        static SlotExtra leaf(Rid rid) {
            return {.rid = rid};
        }
    };

    using NodePage = SlottedPage<NodeExtra, SlotExtra, Value>;

    Engine& eng;
    FileId fid;

    [[nodiscard]] static bool is_leaf(const NodeExtra& node);

    [[nodiscard]] static std::pair<u32, pnum_t> inner_find_child(const NodePage& page,
                                                                 const Value& pkey);

    [[nodiscard]] static u32 leaf_lower_bound_idx(const NodePage& page, const Value& pkey);

    [[nodiscard]] std::pair<pnum_t, pnum_t> leaf_insert_split(pnum_t og_pnum,
                                                              u32 ins_idx,
                                                              const Value& pkey,
                                                              Rid rid);

    [[nodiscard]] std::tuple<pnum_t, Value, pnum_t> inner_insert_split(pnum_t og_pnum,
                                                                       u32 ins_idx,
                                                                       const Value& ins_pkey,
                                                                       pnum_t ins_left_child);

    bool leaf_try_borrow(NodePage& leaf_page, NodePage& par_page, u32 par_idx);

    [[nodiscard]] pnum_t static child_pnum(const NodePage& page, u32 child_idx);

public:
    class RangeCursor {
    private:
        FileId fid;
        BufferManager& buf_mgr;
        std::optional<NodePage> page = std::nullopt;
        pnum_t cur_pnum;
        u32 cur_slot;
        Value pkey_high;
        bool finished = false;

    public:
        explicit RangeCursor(BufferManager& buf_mgr);
        RangeCursor(BufferManager& buf_mgr, pnum_t init_page, u32 init_slot, Value pkey_high);

        std::optional<Rid> next();
    };

    BTreeIndex(Engine& engine, FileId fid);

    void ugly_print() const;
    void init();

    bool add(const Value& pkey, Rid rid);

    [[nodiscard]] std::optional<Rid> search(const Value& pkey);

    [[nodiscard]] RangeCursor range_search(const Value& pkey_low, const Value& pkey_high);

    bool remove(const Value& pkey);
};

#endif
