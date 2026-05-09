#ifndef RECORD_PAGE_HPP
#define RECORD_PAGE_HPP

#include <cassert>
#include <cstddef>
#include <type_traits>
#include <utility>
#include "engine/buffer_manager.hpp"
#include "engine/file_manager.hpp"
#include "types.hpp"
#include "util.hpp"

template<typename T, typename HeaderExtra>
    requires std::is_trivially_copyable_v<T> && std::is_default_constructible_v<HeaderExtra>
class RecordPage {
private:
    using PageGuard = BufferManager::PageGuard;

    struct Header {
        u32 count = 0;
        HeaderExtra extra;
    };

    static constexpr std::size_t MAX_COUNT = (PAGE_SIZE - sizeof(Header)) / sizeof(T);

    PageGuard page;
    Header hdr;
    bool is_dirty = false;

    [[nodiscard]] static u32 entry_offset(u32 entry_idx) {
        return sizeof(Header) + (entry_idx * sizeof(T));
    }

public:
    [[nodiscard]] static std::size_t capacity() {
        return (PAGE_SIZE - sizeof(Header)) / sizeof(T);
    }

    explicit RecordPage(PageGuard page)
        : page{std::move(page)},
          hdr{util::span_read<Header>(this->page.const_data(), 0)} {}

    RecordPage(const RecordPage&) = delete;
    RecordPage(RecordPage&&) = default;

    ~RecordPage() {
        if (is_dirty)
            util::span_write(page.data(), 0, hdr);
    }

    RecordPage& operator=(const RecordPage&) = delete;
    RecordPage& operator=(RecordPage&&) = default;

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

    [[nodiscard]] constexpr u32 count() const {
        return hdr.count;
    }

    [[nodiscard]] constexpr bool is_full() const {
        assert(hdr.count <= MAX_COUNT);
        return hdr.count == MAX_COUNT;
    }

    [[nodiscard]] constexpr T read(u32 entry_idx) const {
        if (entry_idx >= hdr.count)
            throw std::out_of_range("entry index out of range");

        return util::span_read<T>(page.const_data(), entry_offset(entry_idx));
    }

    constexpr void write(u32 entry_idx, const T& val) const {
        if (entry_idx >= hdr.count)
            throw std::out_of_range("entry index out of range");

        return util::span_write<T>(page.data(), entry_offset(entry_idx), val);
    }

    constexpr void push_back(const T& val) {
        insert(hdr.count, std::move(val));
    }

    void insert(u32 slot_idx, const T& data) {
        assert(slot_idx <= hdr.count);

        if (is_full())
            throw std::runtime_error("not enough space");

        // shift slots to the right
        for (std::size_t i = hdr.count; i > slot_idx; --i)
            write(i, read(i - 1));

        hdr.count += 1;
        is_dirty = true;

        write(slot_idx, data);
    }

    void remove(u32 entry_idx) {
        assert(entry_idx < hdr.count);

        // shift slots to the left
        for (u32 i = entry_idx; i < hdr.count - 1; ++i)
            write(i, read(i + 1));

        hdr.count -= 1;
        is_dirty = true;
    }

    void swap_remove(u32 entry_idx) {
        assert(entry_idx < hdr.count);

        auto last = read(hdr.count - 1);
        write(entry_idx, last);

        hdr.count -= 1;
        is_dirty = true;
    }

    void clear() {
        if (hdr.count == 0)
            return;

        hdr.count = 0;
        is_dirty = true;
    }
};

#endif
