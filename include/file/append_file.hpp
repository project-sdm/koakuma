#ifndef APPEND_FILE_HPP
#define APPEND_FILE_HPP

#include <optional>
#include <utility>
#include <vector>
#include "engine/engine.hpp"
#include "engine/file_manager.hpp"
#include "file/common.hpp"
#include "layout/slotted_page.hpp"
#include "util.hpp"

struct AppendHeader {
    UnknownFile::Header hdr;
    pnum_t used_pages = 0;

    AppendHeader();
    explicit AppendHeader(std::vector<Column> columns);
};

template<>
struct pack::PackSize<AppendHeader> {
    std::size_t operator()(const AppendHeader& hdr) const {
        return pack_size<>(hdr.hdr) + pack_size<>(hdr.used_pages);
    }
};

template<>
struct pack::Pack<AppendHeader> {
    void operator()(const AppendHeader& hdr, u8*& dest) const {
        pack<>(hdr.hdr, dest);
        pack<>(hdr.used_pages, dest);
    }
};

template<>
struct pack::Unpack<AppendHeader> {
    void operator()(AppendHeader& dest, const u8*& src) const {
        unpack<>(dest.hdr, src);
        unpack<>(dest.used_pages, src);
    }
};

class AppendFile {
private:
    struct PageHeader {};
    struct SlotExtra {};

    using AppendPage = SlottedPage<PageHeader, SlotExtra, Row>;

    Engine& eng;
    FileId fid;

public:
    class Cursor {
    private:
        AppendFile& append_file;
        AppendHeader append_hdr;
        std::optional<AppendPage> page_buf = std::nullopt;
        u32 cur_slot = 0;
        pnum_t cur_pnum = 1;

        AppendPage& page();

    public:
        using value_type = std::pair<Rid, Row>;

        explicit Cursor(AppendFile& append_file);

        std::optional<value_type> next();
    };

    static_assert(util::iter<Cursor>);

    AppendFile(Engine& engine, FileId fid);

    void init(std::vector<Column> columns);

    [[nodiscard]] Row read_rid(Rid rid) const;
    Rid add(const Row& row);

    Cursor cursor();
};

#endif
