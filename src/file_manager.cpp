#include "file_manager.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <ios>
#include "types.hpp"

FileId::FileId(u64 value)
    : id{value} {}

bool FileId::operator==(const FileId& other) const = default;

constexpr long FileManager::page_offset(pnum_t pnum) {
    return static_cast<long>(PAGE_SIZE) * pnum;
}

std::fstream FileManager::open_create(const std::string& filename) {
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

    file.seekg(page_offset(pnum), std::ios::beg);
    file.read(reinterpret_cast<char*>(data.data()),
              static_cast<std::streamsize>(data.size_bytes()));

    if (!file.good()) {
        file.clear();
        return false;
    }

    return true;
}

void FileManager::write_page(std::fstream& file, pnum_t pnum, std::span<const u8> data) {
    assert(data.size_bytes() == PAGE_SIZE);

    file.seekp(page_offset(pnum), std::ios::beg);
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size_bytes()));
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

    std::ranges::fill(buf, 0);
    std::memcpy(buf.data(), &inactive_page, sizeof(inactive_page));
    write_page(file, pnum, buf);
}

FileId FileManager::open(const std::string& filename) {
    FileId fid{gen_id()};
    open_files.emplace(fid, open_create(filename));
    return fid;
}

void FileManager::close(const FileId& fid) {
    std::size_t deleted = open_files.erase(fid);

    if (deleted == 0)
        throw std::runtime_error("file is not open");
}

pnum_t FileManager::alloc_page(const FileId& fid) {
    auto& file = open_files.at(fid);

    FileHeader header = read_file_header(file);
    assert(header.first_free <= header.page_capacity);

    pnum_t new_page = header.first_free;

    if (header.first_free == header.page_capacity) {
        header.page_capacity += 1;
        header.first_free = header.page_capacity;
    } else {
        auto free_page = read_inactive_page(file, header.first_free);
        header.first_free = free_page.next_free;
    }

    write_file_header(file, header);
    return new_page;
}

void FileManager::free_page(const FileId& fid, pnum_t pnum) {
    assert(pnum != FILE_HEADER_PAGE);
    auto& file = open_files.at(fid);

    FileHeader header = read_file_header(file);
    write_inactive_page(file, pnum, InactivePage{header.first_free});

    header.first_free = pnum;
    write_file_header(file, header);
}

bool FileManager::read_page(const FileId& fid, pnum_t pnum, std::span<u8> data) {
    assert(pnum != FILE_HEADER_PAGE);

    auto& file = open_files.at(fid);
    return read_page(file, pnum, data);
}

void FileManager::write_page(const FileId& fid, pnum_t pnum, std::span<const u8> data) {
    assert(pnum != FILE_HEADER_PAGE);

    auto& file = open_files.at(fid);
    write_page(file, pnum, data);
}
