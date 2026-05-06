#include "engine/file_manager.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <utility>
#include "types.hpp"
#include "util.hpp"

FileId::FileId() = default;

FileId::FileId(u64 value)
    : id{value} {}

bool FileId::operator==(const FileId& other) const = default;

FileManager::OpenFile::OpenFile(std::fstream stream, std::filesystem::path path)
    : stream{std::move(stream)},
      path{std::move(path)} {}

constexpr long FileManager::page_offset(pnum_t pnum) {
    return static_cast<long>(PAGE_SIZE * pnum);
}

std::optional<std::fstream> FileManager::open_inner(const std::filesystem::path& filename) {
    std::fstream file{filename, std::ios::in | std::ios::out | std::ios::binary};

    if (!file.is_open())
        return std::nullopt;

    return file;
}

std::fstream FileManager::open_create_inner(const std::filesystem::path& filename) {
    std::fstream file{filename, std::ios::in | std::ios::out | std::ios::binary};

    // create if not existing
    if (!file.is_open()) {
        file.open(filename, std::ios::out | std::ios::binary);
        file.close();
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    }

    return file;
}

bool FileManager::read_page(std::fstream& file, pnum_t pnum, std::span<u8> data) {
    assert(data.size_bytes() == PAGE_SIZE);
    assert(file.good());

    file.seekg(page_offset(pnum), std::ios::beg);
    file.read(reinterpret_cast<char*>(data.data()),
              static_cast<std::streamsize>(data.size_bytes()));

    read_counter += 1;

    if (!file.good()) {
        file.clear();
        return false;
    }

    return true;
}

void FileManager::write_page(std::fstream& file, pnum_t pnum, std::span<const u8> data) {
    assert(data.size_bytes() == PAGE_SIZE);
    assert(file.good());

    file.seekp(page_offset(pnum), std::ios::beg);
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size_bytes()));
    file.flush();

    write_counter += 1;
}

constexpr u64 FileManager::gen_id() {
    return cur_id++;
}

FileManager::FileHeader FileManager::read_file_header(std::fstream& file) {
    if (!read_page(file, FILE_HEADER_PAGE, buf))
        return FileHeader{};

    FileHeader header;
    std::memcpy(&header, buf.data(), sizeof(header));
    return header;
}

void FileManager::write_file_header(std::fstream& file, const FileHeader& header) {
    if (!read_page(file, FILE_HEADER_PAGE, buf))
        std::ranges::fill(buf, 0);

    std::memcpy(buf.data(), &header, sizeof(header));
    write_page(file, FILE_HEADER_PAGE, buf);
}

FileManager::InactivePage FileManager::read_inactive_page(std::fstream& file, pnum_t pnum) {
    assert(pnum != FILE_HEADER_PAGE);

    if (!read_page(file, pnum, buf))
        throw std::runtime_error("tried to read non-existing inactive page");

    InactivePage out{};
    std::memcpy(&out, buf.data(), sizeof(out));
    return out;
}

void FileManager::write_inactive_page(std::fstream& file,
                                      pnum_t pnum,
                                      const InactivePage& inactive_page) {
    assert(pnum != FILE_HEADER_PAGE);

    std::ranges::fill(buf, 0);  // for security
    std::memcpy(buf.data(), &inactive_page, sizeof(inactive_page));
    write_page(file, pnum, buf);
}

std::optional<FileId> FileManager::open(std::filesystem::path filename) {
    std::fstream file = TRY_OPT(open_inner(filename));
    FileId fid{gen_id()};
    open_files.emplace(fid, OpenFile{std::move(file), std::move(filename)});
    return fid;
}

FileId FileManager::open_create(std::filesystem::path filename) {
    FileId fid{gen_id()};
    open_files.emplace(fid, OpenFile{open_create_inner(filename), std::move(filename)});
    return fid;
}

FileId FileManager::open_copy(FileId fid, std::filesystem::path filename) {
    auto& file = open_files.at(fid);

    std::filesystem::remove(filename);
    std::filesystem::copy_file(file.path, filename);

    return *open(std::move(filename));
}

void FileManager::close(const FileId& fid) {
    std::size_t deleted = open_files.erase(fid);

    if (deleted == 0)
        throw std::runtime_error("file is not open");
}

void FileManager::init_file(FileId fid) {
    auto& file = open_files.at(fid);
    write_file_header(file.stream, FileHeader{});
}

pnum_t FileManager::alloc_page(const FileId& fid) {
    auto& file = open_files.at(fid);

    FileHeader header = read_file_header(file.stream);
    assert(header.first_free <= header.page_capacity);

    pnum_t new_page = header.first_free;

    if (header.first_free == header.page_capacity) {
        header.page_capacity += 1;
        header.first_free = header.page_capacity;
    } else {
        auto free_page = read_inactive_page(file.stream, new_page);
        header.first_free = free_page.next_free;
    }

    write_file_header(file.stream, header);
    return new_page;
}

void FileManager::free_page(const FileId& fid, pnum_t pnum) {
    assert(pnum != FILE_HEADER_PAGE);
    auto& file = open_files.at(fid);

    FileHeader header = read_file_header(file.stream);
    write_inactive_page(file.stream, pnum, InactivePage{header.first_free});

    header.first_free = pnum;
    write_file_header(file.stream, header);
}

bool FileManager::read_page(const FileId& fid, pnum_t pnum, std::span<u8> data) {
    assert(pnum != FILE_HEADER_PAGE);

    auto& file = open_files.at(fid);
    return read_page(file.stream, pnum, data);
}

void FileManager::write_page(const FileId& fid, pnum_t pnum, std::span<const u8> data) {
    assert(pnum != FILE_HEADER_PAGE);

    auto& file = open_files.at(fid);
    write_page(file.stream, pnum, data);
}

bool FileManager::exists(const std::filesystem::path& filename) {
    return std::filesystem::exists(filename);
}

const std::filesystem::path& FileManager::file_path(FileId fid) const {
    const auto& file = open_files.at(fid);
    return file.path;
}

u64 FileManager::get_read_counter() const {
    return read_counter;
}

u64 FileManager::get_write_counter() const {
    return write_counter;
}

void FileManager::reset_rw_counters() {
    read_counter = 0;
    write_counter = 0;
}
