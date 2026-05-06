#ifndef HASH_HPP
#define HASH_HPP

#include <optional>
#include <variant>
#include <vector>
#include "engine/buffer_manager.hpp"
#include "engine/engine.hpp"
#include "engine/file_manager.hpp"
#include "layout/slotted_page.hpp"
#include "pack.hpp"
#include "seq_file.hpp"
#include "types.hpp"
#include "util.hpp"

using HashValue = std::variant<int, bool, std::string>;

// extendible hashing
class HashIndex {
private:
    struct HashHeader {
        std::vector<pnum_t> dir;
        u8 global_depth = 0;
    };

    using BucketHeader = std::variant<pnum_t, u8>;

    struct SlotHeader {
        Rid rid;
    };

    Engine& eng;
    FileId fid;

    using BucketPage = SlottedPage<BucketHeader, SlotHeader, HashValue>;

    friend struct pack::PackSize<HashHeader>;
    friend struct pack::Pack<HashHeader>;
    friend struct pack::Unpack<HashHeader>;

public:
    class Cursor {
        std::optional<BucketPage> page = std::nullopt;

        FileId fid;
        BufferManager& buf_mgr;
        pnum_t cur_pnum;
        u32 cur_slot = 0;
        HashValue search_key;

    public:
        using value_type = Rid;

        Cursor(BufferManager& buf_mgr, FileId fid, pnum_t init_pnum, HashValue search_key);

        std::optional<value_type> next();
    };

    static_assert(util::iter<Cursor>);

    HashIndex(Engine& eng, FileId fid);

    void init();

    void add(const HashValue& pkey, Rid rid);

    [[nodiscard]] Cursor search(const HashValue& pkey);

    bool remove(const HashValue& pkey);

    void ugly_print() const;
};

template<>
struct pack::PackSize<HashIndex::HashHeader> {
    std::size_t operator()(const HashIndex::HashHeader& hdr) const {
        return pack_size(hdr.dir) + pack_size(hdr.global_depth);
    }
};

template<>
struct pack::Pack<HashIndex::HashHeader> {
    void operator()(const HashIndex::HashHeader& hdr, u8*& dest) const {
        pack(hdr.dir, dest);
        pack(hdr.global_depth, dest);
    }
};

template<>
struct pack::Unpack<HashIndex::HashHeader> {
    void operator()(HashIndex::HashHeader& hdr, const u8*& src) const {
        unpack(hdr.dir, src);
        unpack(hdr.global_depth, src);
    }
};

template<>
struct std::formatter<HashValue, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const HashValue& val, std::format_context& ctx) {
        return std::visit([&](auto&& v) { return std::format_to(ctx.out(), "{}", v); }, val);
    }
};

#endif
