#include <cassert>
#include <fstream>
#include <iterator>
#include <print>
#include <system_error>
#include <vector>
#include "buffer_manager.hpp"
#include "file_manager.hpp"
#include "parser/parser.hpp"
#include "seq_file.hpp"

int main() {
    std::println(R"( _  __           _                          )");
    std::println(R"(| |/ /___   __ _| | ___   _ _ __ ___   __ _ )");
    std::println(R"(| ' // _ \ / _` | |/ / | | | '_ ` _ \ / _` |)");
    std::println(R"(| . \ (_) | (_| |   <| |_| | | | | | | (_| |)");
    std::println(R"(|_|\_\___/ \__,_|_|\_\\__,_|_| |_| |_|\__,_|)");
    std::println();
    std::println("Page size: {}", PAGE_SIZE);

    FileManager file_mgr;
    FileId fid = file_mgr.open("seq.bin");

    BufferManager buf_mgr{file_mgr};
    SeqFile seq_file(file_mgr, buf_mgr, fid);

    std::vector<Column> columns = {
        {"foo",    ColumnType::INT},
        {"bar",   ColumnType::BOOL},
        {"baz", ColumnType::STRING},
    };

    // seq_file.init(std::move(columns), 0);

    seq_file.insert({42, true, "hello"});
    seq_file.insert({43, false, "goodbye"});
    seq_file.insert({60, true, "foo"});
    seq_file.insert({63, true, "bar"});

    auto r1_loaded = seq_file.find_by_pkey(42);
    assert(r1_loaded);

    std::println("{}", r1_loaded);
    std::println();

    auto cursor = seq_file.cursor();

    while (auto row = cursor.next())
        std::println("{}", row);

    return 0;

    std::ifstream file{"./data/test.sql"};
    if (!file.is_open()) {
        std::println("{}", std::make_error_code(std::errc(errno)).message());
        return EXIT_FAILURE;
    }

    std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    file.close();

    parser::Parser parser{source};

    auto res = parser.source_file();
    if (!res.has_value())
        std::println("{}", res.error());
    else
        std::println("{}", res.value());
}
