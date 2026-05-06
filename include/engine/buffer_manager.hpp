#ifndef BUFFER_MANAGER_HPP
#define BUFFER_MANAGER_HPP

#include <cassert>
#include <cstddef>
#include <functional>
#include <list>
#include <optional>
#include "file_manager.hpp"

struct PageId {
    FileId fid;
    pnum_t pnum{};

    PageId();
    PageId(FileId fid, pnum_t pnum);

    [[nodiscard]] bool operator==(const PageId& other) const;

    friend class std::hash<PageId>;
};

template<>
struct std::formatter<PageId, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const PageId& pid, std::format_context& ctx) {
        return std::format_to(ctx.out(), "PageId {{ pid = {}, pnum = {} }}", pid.fid, pid.pnum);
    }
};

template<>
class std::hash<PageId> {
public:
    std::size_t operator()(const PageId& pid) const {
        return std::hash<FileId>{}(pid.fid) ^ (pid.pnum << 1);
    }
};

class BufferManager {
private:
    static constexpr std::size_t NUM_FRAMES = 8;

    struct Frame {
        std::size_t num;
    };

    struct FrameMeta {
        PageId pid;
        std::size_t pin_count = 0;
        bool is_dirty = false;
        std::list<PageId>::iterator lru_it;

        FrameMeta();
        explicit FrameMeta(PageId pid, std::list<PageId>::iterator lru_it);
    };

    FileManager& file_mgr;

    std::unordered_map<PageId, std::size_t> page_table;
    std::vector<std::size_t> free_frames;

    std::array<FrameMeta, NUM_FRAMES> frame_meta;
    std::vector<u8> frame_data;

    std::list<PageId> lru_list;

    void load_page(const PageId& pid, std::size_t frame_num);

    std::span<u8> frame_span(std::size_t frame_num);

    auto find_evictable_frame();
    void evict_one_frame();
    std::size_t obtain_free_frame();

public:
    class PageGuard {
    private:
        BufferManager& mgr;
        std::optional<std::size_t> frame_num = std::nullopt;

        [[nodiscard]] FrameMeta& meta() const;

    public:
        PageGuard(BufferManager& mgr, std::size_t frame_num);

        ~PageGuard();

        PageGuard(const PageGuard& other) = delete;
        PageGuard(PageGuard&& other) noexcept;

        PageGuard& operator=(const PageGuard& other) = delete;
        PageGuard& operator=(PageGuard&& other) noexcept;

        [[nodiscard]] std::span<u8> data() const;
        [[nodiscard]] std::span<const u8> const_data() const;

        void mark_dirty() const;

        void reset();
    };

    explicit BufferManager(FileManager& file_mgr);

    BufferManager(const BufferManager&) = delete;
    BufferManager(BufferManager&&) = default;

    BufferManager& operator=(const BufferManager&) = delete;
    BufferManager& operator=(BufferManager&&) = delete;

    ~BufferManager();

    PageGuard fetch_page(const FileId& fid, pnum_t pnum);

    bool flush_page(const PageId& pid);

    void flush_all();
};

#endif
