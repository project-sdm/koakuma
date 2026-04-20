#ifndef FILE_MANAGER_HPP
#define FILE_MANAGER_HPP

#include <cstddef>
#include <fstream>
#include <functional>
#include <vector>
#include "cross.hpp"
#include "types.hpp"

static const std::size_t PAGE_SIZE = cross::get_page_size();
// static const std::size_t PAGE_SIZE = cross::get_page_size();

using pnum_t = long;

class FileId {
private:
    u64 id;

public:
    explicit FileId(u64 value);

    [[nodiscard]] bool operator==(const FileId& other) const;

    friend class std::hash<FileId>;
};

template<>
class std::hash<FileId> {
public:
    std::size_t operator()(const FileId& fid) const {
        return std::hash<u64>{}(fid.id);
    }
};

class FileManager {
private:
    struct FileHeader {
        pnum_t page_capacity = 1;
        pnum_t first_free = 1;
    };

    struct InactivePage {
        pnum_t next_free;
    };

    static constexpr pnum_t FILE_HEADER_PAGE = 0;

    u64 cur_id = 0;
    std::unordered_map<FileId, std::fstream> open_files;
    std::vector<u8> buf{std::vector<u8>(PAGE_SIZE)};

    [[nodiscard]] static constexpr long page_offset(pnum_t pnum);

    std::fstream open_create(const std::string& filename);

    [[nodiscard]] static bool read_page(std::fstream& file, pnum_t pnum, std::span<u8> data);
    static void write_page(std::fstream& file, pnum_t pnum, std::span<const u8> data);

    constexpr u64 gen_id();

    [[nodiscard]] FileHeader read_file_header(std::fstream& file);
    void write_file_header(std::fstream& file, const FileHeader& header);

    [[nodiscard]] InactivePage read_inactive_page(std::fstream& file, pnum_t pnum);
    void write_inactive_page(std::fstream& file, pnum_t pnum, const InactivePage& page);

public:
    // NOTE: we might want to return a FileGuard or something similar.
    // It depends on how long-lived files will be in practice.
    [[nodiscard]] FileId open(const std::string& filename);
    void close(const FileId& fid);

    [[nodiscard]] pnum_t alloc_page(const FileId& fid);
    void free_page(const FileId& fid, pnum_t pnum);

    [[nodiscard]] bool read_page(const FileId& fid, pnum_t pnum, std::span<u8> data);
    void write_page(const FileId& fid, pnum_t pnum, std::span<const u8> data);
};

#endif
