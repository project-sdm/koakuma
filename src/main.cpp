#include <cassert>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <print>
#include <system_error>
#include "buffer_manager.hpp"
#include "file_manager.hpp"
#include "parser/parser.hpp"

int main() {
    std::println("Page size: {}", PAGE_SIZE);

    FileManager file_mgr;
    BufferManager<8> buf_mgr{file_mgr};

    auto fid = file_mgr.open("./data_1.bin");

    auto pnum = file_mgr.alloc_page(fid);

    {
        auto pg = buf_mgr.fetch_page(fid, pnum);
        auto span = pg.data();

        for (std::size_t i = 0; i < PAGE_SIZE; ++i)
            span[i] = pnum + i;
    }

    buf_mgr.flush_all();

    return 0;

    std::ifstream file{"./data/test.sql"};
    if (!file.is_open()) {
        std::println("{}", std::make_error_code(std::errc(errno)).message());
        return EXIT_FAILURE;
    }

    std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    file.close();

    Parser parser{source};
    auto res = parser.source_file();
    if (!res.has_value())
        std::println("{}", res.error());
    else
        std::println("{}", res.value());
}
