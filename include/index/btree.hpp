#ifndef BTREE_HPP
#define BTREE_HPP

// (actually a b+ tree)
#include <optional>
#include <print>
#include <tuple>
#include <utility>
#include "engine/buffer_manager.hpp"
#include "engine/engine.hpp"
#include "engine/file_manager.hpp"
#include "seq_file.hpp"

using PageGuard = BufferManager::PageGuard;

class BTreeIndex {
private:
    struct BTreeHeader {
        pnum_t root = PAGE_NIL;
    };

    struct PageHeader {
        u32 slot_cnt = 0;
        u32 data_begin = PAGE_SIZE;
        pnum_t last_child = PAGE_NIL;
        pnum_t next_page = PAGE_NIL;
        pnum_t prev_page = PAGE_NIL;
    };

    class Slot {
    private:
        u32 pos;
        u32 len;

    public:
        union {
            pnum_t left_child;
            Rid rid;
        };

        Slot(u32 pos, u32 len, pnum_t left_child);
        Slot(u32 pos, u32 len, Rid rid);

        // TODO: remove somehow
        [[nodiscard]] u32 data_pos() const;
    };

    class SlottedPage {
    private:
        PageGuard page;
        PageHeader hdr;
        bool is_dirty = false;

        template<typename T>
        void push_back(const Value& pkey, T val);

        template<typename T>
        void insert(u32 slot_idx, const Value& pkey, T val);

    public:
        explicit SlottedPage(PageGuard page);
        SlottedPage(const SlottedPage&) = delete;
        SlottedPage(SlottedPage&&) = delete;

        ~SlottedPage();

        SlottedPage& operator=(const SlottedPage&) = delete;
        SlottedPage& operator=(SlottedPage&&) = delete;

        void init();

        [[nodiscard]] PageHeader& header();
        [[nodiscard]] const PageHeader& const_header() const;
        [[nodiscard]] u32 slot_cnt() const;

        [[nodiscard]] static u32 slot_offset(u32 slot_idx);

        [[nodiscard]] Slot read_slot(u32 slot_idx) const;
        void write_slot(u32 slot_idx, const Slot& slot);

        [[nodiscard]] u32 total_space() const;
        [[nodiscard]] u32 free_space() const;
        [[nodiscard]] bool will_fit(const Value& pkey) const;

        [[nodiscard]] Value read_key(const Slot& slot) const;
        [[nodiscard]] Value read_key(u32 slot_idx) const;

        void leaf_push_back(const Value& pkey, Rid rid);
        void leaf_insert(u32 slot_idx, const Value& pkey, Rid rid);

        void inner_push_back(const Value& pkey, pnum_t left_child);
        void inner_insert(u32 slot_idx, const Value& pkey, pnum_t left_child);
    };

    Engine& eng;
    FileId fid;

    [[nodiscard]] static bool is_leaf(const PageHeader& hdr);

    [[nodiscard]] static std::pair<u32, pnum_t> inner_find_child(const SlottedPage& page,
                                                                 const Value& pkey);

    [[nodiscard]] static u32 leaf_lower_bound_idx(const SlottedPage& page, const Value& pkey);

    [[nodiscard]] std::pair<pnum_t, pnum_t> leaf_insert_split(pnum_t og_pnum,
                                                              u32 ins_idx,
                                                              const Value& pkey,
                                                              Rid rid);

    [[nodiscard]] std::tuple<pnum_t, Value, pnum_t> inner_insert_split(pnum_t og_pnum,
                                                                       u32 ins_idx,
                                                                       const Value& ins_pkey,
                                                                       pnum_t ins_left_child);

public:
    BTreeIndex(Engine& engine, FileId fid);

    void ugly_print() const;
    void init();

    bool insert(const Value& pkey, Rid rid);
    [[nodiscard]] std::optional<Rid> search(const Value& pkey);
    bool remove(const Value& pkey);
};

template<typename T>
void BTreeIndex::SlottedPage::push_back(const Value& pkey, T val) {
    insert<T>(hdr.slot_cnt, pkey, std::move(val));
}

template<typename T>
void BTreeIndex::SlottedPage::insert(u32 idx, const Value& pkey, T val) {
    assert(idx <= hdr.slot_cnt);

    u32 pkey_size = pack::pack_size(pkey);
    assert(free_space() >= sizeof(Slot) + pkey_size);

    is_dirty = true;

    // shift slots to the right
    for (std::size_t i = hdr.slot_cnt; i > idx; --i) {
        Slot slot = read_slot(i - 1);
        write_slot(i, slot);
    }

    u32 pkey_pos = hdr.data_begin - pkey_size;
    Slot new_slot{pkey_pos, pkey_size, std::move(val)};

    u8* dest = page.data().subspan(pkey_pos).data();
    pack::pack(pkey, dest);

    write_slot(idx, new_slot);

    hdr.slot_cnt += 1;
    hdr.data_begin = pkey_pos;
}

#endif
