#ifndef FILE_MANAGER_HPP
#define FILE_MANAGER_HPP

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <vector>
#include "pack.hpp"
#include "types.hpp"

static const std::size_t PAGE_SIZE = 1024;
// static const std::size_t PAGE_SIZE = cross::get_page_size();

using pnum_t = u64;

class FileId {
private:
    u64 id{};

public:
    FileId();
    explicit FileId(u64 value);

    [[nodiscard]] bool operator==(const FileId& other) const;

    friend class std::hash<FileId>;
    friend class std::formatter<FileId, char>;
};

template<>
struct std::formatter<FileId, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(const FileId& val, std::format_context& ctx) {
        return std::format_to(ctx.out(), "FileId{{{}}}", val.id);
    }
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
        bool user_head_init = false;
    };

    struct InactivePage {
        pnum_t next_free;
    };

    static constexpr pnum_t FILE_HEADER_PAGE = 0;

    u64 cur_id = 0;
    std::unordered_map<FileId, std::fstream> open_files;
    std::vector<u8> buf{std::vector<u8>(PAGE_SIZE)};

    [[nodiscard]] static constexpr long page_offset(pnum_t pnum);

    static std::optional<std::fstream> open_inner(const std::filesystem::path& filename);
    static std::fstream open_create_inner(const std::filesystem::path& filename);

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
    [[nodiscard]] FileId open_create(const std::filesystem::path& filename);
    [[nodiscard]] std::optional<FileId> open(const std::filesystem::path& filename);
    void close(const FileId& fid);

    [[nodiscard]] static bool exists(const std::filesystem::path& filename);

    [[nodiscard]] pnum_t alloc_page(const FileId& fid);
    void free_page(const FileId& fid, pnum_t pnum);

    [[nodiscard]] bool read_page(const FileId& fid, pnum_t pnum, std::span<u8> data);
    void write_page(const FileId& fid, pnum_t pnum, std::span<const u8> data);

    template<typename UserHeader>
        requires std::is_default_constructible_v<UserHeader>
    UserHeader read_user_header(const FileId& fid);

    template<typename UserHeader>
    void write_user_header(const FileId& fid, const UserHeader& usr_header);
};

template<typename UserHeader>
    requires std::is_default_constructible_v<UserHeader>
UserHeader FileManager::read_user_header(const FileId& fid) {
    auto& file = open_files.at(fid);
    FileHeader header = read_file_header(file);

    if (!header.user_head_init)
        return UserHeader{};

    // NOTE: this relies on read_file_header leaving `buf` with the first page.
    return pack::unpack_alloc<UserHeader>(buf.data() + sizeof(FileHeader));
}

template<typename UserHeader>
void FileManager::write_user_header(const FileId& fid, const UserHeader& usr_header) {
    if (sizeof(FileHeader) + pack::pack_size<>(usr_header) > PAGE_SIZE)
        throw std::runtime_error("user header is too large for a single page");

    auto& file = open_files.at(fid);
    FileHeader header = read_file_header(file);
    header.user_head_init = true;

    // NOTE: this also relies on read_file_header leaving `buf` with the first page.
    std::memcpy(buf.data(), &header, sizeof(header));

    u8* dest = buf.data() + sizeof(FileHeader);
    pack::pack(usr_header, dest);
    write_page(file, FILE_HEADER_PAGE, buf);
}

#endif
