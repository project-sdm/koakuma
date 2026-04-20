#ifndef BUFFER_MANAGER_HPP
#define BUFFER_MANAGER_HPP

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <list>
#include "file_manager.hpp"

struct PageId {
    FileId fid;
    pnum_t pnum;

    PageId(FileId fid, pnum_t pnum);

    [[nodiscard]] bool operator==(const PageId& other) const;

    friend class std::hash<PageId>;
};

template<>
class std::hash<PageId> {
public:
    std::size_t operator()(const PageId& pid) const {
        return std::hash<FileId>{}(pid.fid) ^ (pid.pnum << 1);
    }
};

template<std::size_t NUM_FRAMES>
class BufferManager {
private:
    struct Frame {
        std::size_t num;
    };

    struct FrameMeta {
        std::size_t pin_count = 0;
        bool is_dirty = false;
        std::list<PageId>::iterator lru_it;

        FrameMeta() = default;

        explicit FrameMeta(std::list<PageId>::iterator lru_it)
            : lru_it{lru_it} {}
    };

    FileManager& file_mgr;

    std::unordered_map<PageId, std::size_t> page_table;
    std::vector<std::size_t> free_frames;

    std::array<FrameMeta, NUM_FRAMES> frame_meta;
    std::vector<u8> frame_data;

    std::list<PageId> lru_list;

    void load_page(const PageId& pid, std::size_t frame_num) {
        FrameMeta& meta = frame_meta[frame_num];
        assert(meta.pin_count == 0);

        meta.is_dirty = false;

        auto span = frame_span(frame_num);

        if (!file_mgr.read_page(pid.fid, pid.pnum, span))
            std::ranges::fill(span, 0);
    }

    std::span<u8> frame_span(std::size_t frame_num) {
        assert(frame_num < NUM_FRAMES);

        std::size_t pos = PAGE_SIZE * frame_num;
        return std::span{&frame_data[pos], PAGE_SIZE};
    }

    auto find_evictable_frame() {
        auto evict_it = lru_list.rbegin();

        while (frame_meta[page_table.at(*evict_it)].pin_count > 0) {
            ++evict_it;

            if (evict_it == lru_list.rend())
                throw std::runtime_error("could not find evictable frame");
        }

        return std::next(evict_it).base();
    }

    void evict_one_frame() {
        auto evict_it = find_evictable_frame();

        PageId evicted_pid = *evict_it;
        flush_page(evicted_pid);

        std::size_t evicted_frame_num = page_table.at(evicted_pid);

        lru_list.erase(evict_it);
        page_table.erase(evicted_pid);

        free_frames.push_back(evicted_frame_num);
    }

    std::size_t obtain_free_frame() {
        if (free_frames.empty())
            evict_one_frame();

        std::size_t frame_num = free_frames.back();
        free_frames.pop_back();
        return frame_num;
    }

public:
    class PageGuard {
    private:
        BufferManager& mgr;
        std::size_t frame_num;

        [[nodiscard]] constexpr FrameMeta& meta() const {
            return mgr.frame_meta[frame_num];
        }

    public:
        PageGuard(BufferManager& mgr, std::size_t frame_num)
            : mgr{mgr},
              frame_num{frame_num} {
            ++meta().pin_count;
        }

        ~PageGuard() {
            --meta().pin_count;
        }

        PageGuard(const PageGuard& other) = delete;
        PageGuard(PageGuard&& other) noexcept = delete;

        PageGuard& operator=(const PageGuard& other) = delete;
        PageGuard& operator=(PageGuard&& other) noexcept = delete;

        [[nodiscard]] constexpr std::span<u8> data() const {
            mark_dirty();  // NOTE: maybe this should be explicitly called only?
            return mgr.frame_span(frame_num);
        }

        [[nodiscard]] constexpr std::span<const u8> data_const() const {
            return mgr.frame_span(frame_num);
        }

        constexpr void mark_dirty() const {
            meta().is_dirty = true;
        }
    };

    explicit BufferManager(FileManager& file_mgr)
        : file_mgr{file_mgr},
          frame_data(NUM_FRAMES * PAGE_SIZE) {
        for (std::size_t i = 0; i < NUM_FRAMES; ++i)
            free_frames.push_back(i);
    }

    BufferManager(const BufferManager&) = delete;
    BufferManager(BufferManager&&) = delete;

    BufferManager& operator=(const BufferManager&) = delete;
    BufferManager& operator=(BufferManager&&) = delete;

    ~BufferManager() {
        flush_all();
    }

    PageGuard fetch_page(const FileId& fid, pnum_t pnum) {
        PageId pid{fid, pnum};
        auto it = page_table.find(pid);

        if (it != page_table.end()) {
            // Hit
            auto frame_num = it->second;
            lru_list.splice(lru_list.begin(), lru_list, frame_meta[frame_num].lru_it);
            return PageGuard{*this, frame_num};
        }

        // Miss
        std::size_t frame_num = obtain_free_frame();
        lru_list.push_front(pid);
        page_table.try_emplace(pid, frame_num);
        frame_meta[frame_num] = FrameMeta{lru_list.begin()};
        load_page(pid, frame_num);

        return PageGuard{*this, frame_num};
    }

    bool flush_page(const PageId& pid) {
        std::size_t frame_num = page_table.at(pid);
        FrameMeta& meta = frame_meta[frame_num];

        if (!meta.is_dirty)
            return false;

        file_mgr.write_page(pid.fid, pid.pnum, frame_span(frame_num));
        meta.is_dirty = false;
        return true;
    }

    void flush_all() {
        for (const auto& [pid, _] : page_table)
            flush_page(pid);
    }
};

#endif
