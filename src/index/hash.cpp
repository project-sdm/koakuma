#include "index/hash.hpp"
#include <cassert>
#include <cstddef>
#include <functional>
#include <optional>
#include <print>
#include <utility>
#include <variant>
#include "engine/buffer_manager.hpp"
#include "engine/file_manager.hpp"
#include "file/seq_file.hpp"
#include "types.hpp"

ValueNotHashable::ValueNotHashable(Value val)
    : val{std::move(val)} {}

std::expected<HashValue, ValueNotHashable> val_to_hash_val(Value val) {
    if (auto* s = std::get_if<std::string>(&val))
        return HashValue{*s};

    if (auto* n = std::get_if<int>(&val))
        return HashValue{*n};

    if (auto* b = std::get_if<bool>(&val))
        return HashValue{*b};

    return std::unexpected{ValueNotHashable{std::move(val)}};
}

// largest depth such that the directory fits within the header page
static constexpr u8 MAX_DEPTH = 8;

HashIndex::HashIndex(Engine& eng, FileId fid)
    : eng{eng},
      fid{fid} {}

void HashIndex::init() {
    eng.file_mgr.init_file(fid);

    HashHeader hdr{};

    pnum_t init_bucket = eng.file_mgr.alloc_page(fid);

    {
        BucketPage init_page{eng.buf_mgr.fetch_page(fid, init_bucket)};
        init_page.init();
        init_page.header_extra() = hdr.global_depth;
    }

    hdr.dir.push_back(init_bucket);
    eng.file_mgr.write_user_header(fid, hdr);
}

void HashIndex::add(const HashValue& key, Rid rid) {
    auto file_hdr = eng.file_mgr.read_user_header<HashHeader>(fid);

    std::size_t h_full = std::hash<HashValue>{}(key);
    std::size_t h_part = h_full % (1 << file_hdr.global_depth);

    pnum_t cur_pnum = file_hdr.dir[h_part];

    std::optional<pnum_t> to_free = std::nullopt;

    while (true) {
        if (to_free) {
            eng.file_mgr.free_page(fid, *to_free);
            to_free.reset();
        }

        BucketPage cur_bucket{eng.buf_mgr.fetch_page(fid, cur_pnum)};

        // case 1: key fits
        // the function ends here
        if (cur_bucket.will_fit(key)) {
            cur_bucket.push_back(SlotHeader{rid}, key);
            return;
        }

        // case 2: this is a chained bucket
        if (auto* next = std::get_if<pnum_t>(&cur_bucket.header_extra())) {
            if (*next == PAGE_NIL) {
                // we need to chain a new bucket
                *next = eng.file_mgr.alloc_page(fid);

                BucketPage new_bucket{eng.buf_mgr.fetch_page(fid, *next)};
                new_bucket.init();
                new_bucket.header_extra() = PAGE_NIL;
            }

            cur_pnum = *next;
            continue;
        }

        // case 3: this is a non-overflowed main bucket

        u8 local_depth = std::get<u8>(cur_bucket.const_header_extra());
        u8 new_depth = local_depth + 1;

        if (new_depth > MAX_DEPTH) {
            // case 3.1: we can't split anymore.
            // we need to begin a chain
            cur_bucket.header_extra() = PAGE_NIL;
            continue;
        }

        // case 3.2: we can split.

        // increase global depth if necessary
        if (new_depth > file_hdr.global_depth) {
            file_hdr.global_depth = new_depth;
            file_hdr.dir.insert(file_hdr.dir.begin(), file_hdr.dir.begin(), file_hdr.dir.end());
        }

        // split
        pnum_t bucket_0 = eng.file_mgr.alloc_page(fid);
        pnum_t bucket_1 = eng.file_mgr.alloc_page(fid);

        {
            BucketPage bucket_0_page{eng.buf_mgr.fetch_page(fid, bucket_0)};
            bucket_0_page.init();
            bucket_0_page.header_extra() = new_depth;

            BucketPage bucket_1_page{eng.buf_mgr.fetch_page(fid, bucket_1)};
            bucket_1_page.init();
            bucket_1_page.header_extra() = new_depth;

            for (u32 i = 0; i < cur_bucket.slot_cnt(); ++i) {
                HashValue key = cur_bucket.read_data(i);

                std::size_t h = std::hash<HashValue>{}(key);

                auto extra = cur_bucket.read_slot_extra(i);

                if ((h & (1 << local_depth)) == 0)
                    bucket_0_page.push_back(extra, key);
                else
                    bucket_1_page.push_back(extra, key);
            }
        }

        file_hdr.dir[h_part & ~(1 << local_depth)] = bucket_0;
        file_hdr.dir[h_part | (1 << local_depth)] = bucket_1;
        eng.file_mgr.write_user_header<HashHeader>(fid, file_hdr);

        to_free = cur_pnum;

        std::size_t h_part_new = h_full % (1 << file_hdr.global_depth);
        cur_pnum = file_hdr.dir[h_part_new];
    }
}

HashIndex::Cursor HashIndex::search(const HashValue& key) {
    auto file_hdr = eng.file_mgr.read_user_header<HashHeader>(fid);

    std::size_t h = std::hash<HashValue>{}(key);
    std::size_t h_part = h % (1 << file_hdr.global_depth);

    return Cursor{eng.buf_mgr, fid, file_hdr.dir[h_part], key};
}

bool HashIndex::remove(const HashValue& key) {
    auto file_hdr = eng.file_mgr.read_user_header<HashHeader>(fid);

    std::size_t h = std::hash<HashValue>{}(key) % (1 << file_hdr.global_depth);
    pnum_t cur_bucket = file_hdr.dir[h];

    while (cur_bucket != PAGE_NIL) {
        BucketPage bucket_page{eng.buf_mgr.fetch_page(fid, cur_bucket)};

        for (u32 i = 0; i < bucket_page.slot_cnt(); ++i) {
            if (key == bucket_page.read_data(i)) {
                bucket_page.swap_remove(i);
                return true;
            }
        }

        if (const auto* next = std::get_if<pnum_t>(&bucket_page.const_header_extra()))
            cur_bucket = *next;
        else
            cur_bucket = PAGE_NIL;
    }

    return false;
}

void HashIndex::ugly_print() const {
    auto file_hdr = eng.file_mgr.read_user_header<HashHeader>(fid);

    std::println("D = {}", file_hdr.global_depth);
    for (std::size_t i = 0; i < file_hdr.dir.size(); ++i) {
        std::print("Bucket {}: ", i);

        pnum_t cur_bucket = file_hdr.dir[i];

        while (cur_bucket != PAGE_NIL) {
            std::print("-> ");

            BucketPage bucket_page{eng.buf_mgr.fetch_page(fid, cur_bucket)};

            for (u32 i = 0; i < bucket_page.slot_cnt(); ++i)
                std::print("{} ", bucket_page.read_data(i));

            if (const auto* next = std::get_if<pnum_t>(&bucket_page.const_header_extra()))
                cur_bucket = *next;
            else
                break;
        }

        std::println();
    }
}

HashIndex::Cursor::Cursor(BufferManager& buf_mgr,
                          FileId fid,
                          pnum_t init_pnum,
                          HashValue search_key)
    : fid{fid},
      buf_mgr{buf_mgr},
      cur_pnum{init_pnum},
      search_key{std::move(search_key)} {}

std::optional<Rid> HashIndex::Cursor::next() {
    while (true) {
        if (cur_pnum == PAGE_NIL)
            return std::nullopt;

        if (!page)
            page = BucketPage{buf_mgr.fetch_page(fid, cur_pnum)};

        assert(cur_slot <= page->slot_cnt());

        if (cur_slot == page->slot_cnt()) {
            page = std::nullopt;
            cur_slot = 0;

            if (const auto* next = std::get_if<pnum_t>(&page->const_header_extra()))
                cur_pnum = *next;
            else
                cur_pnum = PAGE_NIL;

            continue;
        }

        HashValue key = page->read_data(cur_slot);
        auto extra = page->read_slot_extra(cur_slot);

        cur_slot += 1;

        if (key == search_key)
            return extra.rid;
    }
}
