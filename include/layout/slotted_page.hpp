#ifndef SLOTTED_PAGE_HPP
#define SLOTTED_PAGE_HPP

#include <utility>
#include "engine/buffer_manager.hpp"
#include "util.hpp"

template<typename HeaderExtra, typename SlotExtra, typename Data>
class SlottedPage {
private:
    using PageGuard = BufferManager::PageGuard;

    class Slot {
    private:
        u32 pos;
        u32 len;
        SlotExtra extra_data;

    public:
        Slot(u32 pos, u32 len, SlotExtra extra)
            : pos{pos},
              len{len},
              extra_data{std::move(extra)} {}

        SlotExtra& extra() {
            return extra_data;
        }

        const SlotExtra& extra() const {
            return extra_data;
        }

        friend class SlottedPage;
    };

    struct Header {
        u32 slot_cnt = 0;
        u32 data_begin = PAGE_SIZE;
        HeaderExtra extra;
    };

    PageGuard page;
    Header hdr;
    bool is_dirty = false;

    [[nodiscard]] static u32 slot_offset(u32 slot_idx) {
        return sizeof(Header) + (slot_idx * sizeof(Slot));
    }

public:
    explicit SlottedPage(PageGuard page)
        : page{std::move(page)},
          hdr{util::span_read<Header>(page.const_data(), 0)} {}

    SlottedPage(const SlottedPage&) = delete;
    SlottedPage(SlottedPage&&) = delete;

    ~SlottedPage() {
        if (is_dirty)
            util::span_write(page.data(), 0, hdr);
    }

    SlottedPage& operator=(const SlottedPage&) = delete;
    SlottedPage& operator=(SlottedPage&&) = delete;

    void init() {
        is_dirty = true;
        hdr = Header{};
    }

    [[nodiscard]] HeaderExtra& header_extra() {
        return hdr.extra;
    }

    [[nodiscard]] const HeaderExtra& const_header_extra() const {
        return hdr.extra;
    }

    [[nodiscard]] u32 slot_cnt() const {
        return hdr.slot_cnt;
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

    [[nodiscard]] u32 total_space() const {
        return page.const_data().size() - sizeof(Header);
    }

    [[nodiscard]] u32 free_space() const {
        // prevents an underflow that should not happen
        assert(hdr.data_begin >= sizeof(Header) + (hdr.slot_cnt * sizeof(Slot)));

        return hdr.data_begin - sizeof(Header) - (hdr.slot_cnt * sizeof(Slot));
    }

    [[nodiscard]] bool will_fit(const Data& data) const {
        return free_space() >= sizeof(Slot) + pack::pack_size(data);
    }

    [[nodiscard]] Data read_data(const Slot& slot) const {
        return pack::unpack_alloc<Data>(page.const_data().subspan(slot.pos).data());
    }
    [[nodiscard]] Data read_data(u32 slot_idx) const {
        Slot slot = read_slot(slot_idx);
        return read_data(slot);
    }

    void push_back(SlotExtra extra, const Data& data) {
        insert(hdr.slot_cnt, std::move(extra), data);
    }
    void insert(u32 slot_idx, SlotExtra extra, const Data& data) {
        assert(slot_idx <= hdr.slot_cnt);

        u32 data_size = pack::pack_size(data);
        assert(free_space() >= sizeof(Slot) + data_size);

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
    }
};

#endif
