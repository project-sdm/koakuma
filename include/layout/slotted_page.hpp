#ifndef SLOTTED_PAGE_HPP
#define SLOTTED_PAGE_HPP

#include <cassert>
#include <cstring>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>
#include "engine/buffer_manager.hpp"
#include "pack.hpp"
#include "types.hpp"
#include "util.hpp"

template<typename HeaderExtra, typename SlotExtra, typename Data>
class SlottedPage {
public:
    class Slot {
    private:
        u32 pos;
        u32 len;
        SlotExtra extra;

    public:
        Slot(u32 pos, u32 len, SlotExtra extra)
            : pos{pos},
              len{len},
              extra{std::move(extra)} {}

        friend class SlottedPage;
    };

private:
    using PageGuard = BufferManager::PageGuard;

    struct Header {
        u32 slot_cnt = 0;
        u32 data_begin = PAGE_SIZE;
        u32 used_heap = 0;
        HeaderExtra extra;
    };

    PageGuard page;
    Header hdr;
    bool is_dirty = false;

    [[nodiscard]] static u32 slot_offset(u32 slot_idx) {
        return sizeof(Header) + (slot_idx * sizeof(Slot));
    }

    void compact() {
        is_dirty = true;

        // (questionable?) usage of an extra temporary buffer to hold heap
        auto heap_span = page.const_data().subspan(hdr.data_begin);
        std::vector<u8> prev_data{heap_span.begin(), heap_span.end()};

        u32 prev_data_begin = hdr.data_begin;
        hdr.data_begin = page.const_data().size_bytes();

        for (u32 i = 0; i < hdr.slot_cnt; ++i) {
            auto slot = read_slot(i);
            u32 new_pos = hdr.data_begin - slot.len;

            std::span<u8> src = std::span{prev_data}.subspan(slot.pos - prev_data_begin);
            std::span<u8> dest = page.data().subspan(new_pos);

            std::memcpy(dest.data(), src.data(), slot.len);

            slot.pos = new_pos;
            write_slot(i, slot);

            hdr.data_begin = new_pos;
        }
    }

    [[nodiscard]] u32 contiguous_free_space() const {
        // prevents an underflow that should not happen
        assert(hdr.data_begin >= sizeof(Header) + (hdr.slot_cnt * sizeof(Slot)));
        return hdr.data_begin - sizeof(Header) - (hdr.slot_cnt * sizeof(Slot));
    }

    void ensure_contiguous_space(u32 space) {
        assert(free_space() >= space);

        if (contiguous_free_space() < space)
            compact();

        assert(contiguous_free_space() >= space);
    }

    [[nodiscard]] Slot read_slot(u32 slot_idx) const {
        if (slot_idx >= hdr.slot_cnt)
            throw std::out_of_range("slot index out of range");

        return util::span_read<Slot>(page.const_data(), slot_offset(slot_idx));
    }

    void write_slot(u32 slot_idx, const Slot& slot) {
        is_dirty = true;
        util::span_write(page.data(), slot_offset(slot_idx), slot);
    }

public:
    explicit SlottedPage(PageGuard page)
        : page{std::move(page)},
          hdr{util::span_read<Header>(this->page.const_data(), 0)} {}

    SlottedPage(const SlottedPage&) = delete;
    SlottedPage(SlottedPage&&) = default;

    ~SlottedPage() {
        if (is_dirty)
            util::span_write(page.data(), 0, hdr);
    }

    SlottedPage& operator=(const SlottedPage&) = delete;
    SlottedPage& operator=(SlottedPage&&) = default;

    void init() {
        is_dirty = true;
        hdr = Header{};
    }

    [[nodiscard]] constexpr HeaderExtra& header_extra() {
        is_dirty = true;
        return hdr.extra;
    }

    [[nodiscard]] constexpr const HeaderExtra& const_header_extra() const {
        return hdr.extra;
    }

    [[nodiscard]] constexpr u32 slot_cnt() const {
        return hdr.slot_cnt;
    }

    [[nodiscard]] constexpr u32 total_space() const {
        return page.const_data().size_bytes() - sizeof(Header);
    }

    [[nodiscard]] u32 free_space() const {
        assert(total_space() >= used_space());
        return total_space() - used_space();
    }

    [[nodiscard]] constexpr u32 used_space() const {
        return (hdr.slot_cnt * sizeof(Slot)) + hdr.used_heap;
    }

    [[nodiscard]] constexpr bool will_fit(const Data& data) const {
        return free_space() >= sizeof(Slot) + pack::pack_size(data);
    }

    [[nodiscard]] constexpr Data read_data(u32 slot_idx) const {
        Slot slot = read_slot(slot_idx);
        return pack::unpack_alloc<Data>(page.const_data().subspan(slot.pos).data());
    }

    [[nodiscard]] SlotExtra read_slot_extra(u32 slot_idx) const {
        Slot slot = read_slot(slot_idx);
        return std::move(slot.extra);
    }

    void write_slot_extra(u32 slot_idx, SlotExtra extra) {
        Slot slot = read_slot(slot_idx);
        slot.extra = std::move(extra);
        write_slot(slot_idx, slot);
    }

    constexpr void push_back(SlotExtra extra, const Data& data) {
        insert(hdr.slot_cnt, std::move(extra), data);
    }

    void insert(u32 slot_idx, SlotExtra extra, const Data& data) {
        assert(slot_idx <= hdr.slot_cnt);

        u32 data_size = pack::pack_size(data);
        ensure_contiguous_space(sizeof(Slot) + data_size);

        if (!will_fit(data))
            throw std::runtime_error("not enough space");

        is_dirty = true;

        // shift slots to the right
        for (std::size_t i = hdr.slot_cnt; i > slot_idx; --i) {
            Slot slot = read_slot(i - 1);
            write_slot(i, slot);
        }

        u32 data_pos = hdr.data_begin - data_size;
        Slot new_slot{data_pos, data_size, std::move(extra)};

        u8* dest = page.data().subspan(data_pos).data();
        pack::pack(data, dest);

        write_slot(slot_idx, new_slot);

        hdr.slot_cnt += 1;
        hdr.data_begin = data_pos;
        hdr.used_heap += data_size;
    }

    void update_data(u32 slot_idx, const Data& new_data) {
        Slot slot = read_slot(slot_idx);
        u32 prev_len = slot.len;

        u32 data_size = pack::pack_size(new_data);
        is_dirty = true;

        if (data_size <= slot.len) {
            u8* dest = page.data().subspan(slot.pos).data();
            slot.len = data_size;

            pack::pack(new_data, dest);
        } else {
            ensure_contiguous_space(data_size);

            u32 data_pos = hdr.data_begin - data_size;
            u8* dest = page.data().subspan(data_pos).data();

            pack::pack(new_data, dest);

            slot.pos = data_pos;
            slot.len = data_size;

            hdr.data_begin = data_pos;
        }

        hdr.used_heap += data_size;
        hdr.used_heap -= prev_len;

        write_slot(slot_idx, slot);
    }

    void remove(u32 slot_idx) {
        assert(slot_idx < hdr.slot_cnt);
        is_dirty = true;

        Slot removed = read_slot(slot_idx);

        // shift slots to the left
        for (std::size_t i = slot_idx; i < hdr.slot_cnt - 1; ++i) {
            Slot slot = read_slot(i + 1);
            write_slot(i, slot);
        }

        hdr.slot_cnt -= 1;

        assert(hdr.used_heap >= removed.len);
        hdr.used_heap -= removed.len;
    }

    void swap_remove(u32 slot_idx) {
        assert(slot_idx < hdr.slot_cnt);

        auto last = read_slot(hdr.slot_cnt - 1);
        auto removed = read_slot(slot_idx);
        write_slot(slot_idx, last);

        is_dirty = true;
        hdr.slot_cnt -= 1;

        assert(hdr.used_heap >= removed.len);
        hdr.used_heap -= removed.len;
    }
};

#endif
