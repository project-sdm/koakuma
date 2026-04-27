#include <cassert>
#include <format>
#include <print>
#include <vector>
#include "catalog.hpp"
#include "engine/buffer_manager.hpp"
#include "engine/engine.hpp"
#include "engine/file_manager.hpp"
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
    BufferManager buf_mgr{file_mgr};
    Engine eng{};

    std::vector<Column> columns = {
        {"foo",    ColumnType::INT},
        {"bar",   ColumnType::BOOL},
        {"baz", ColumnType::STRING},
    };

    bool created = catalog::create_table(eng, "hello", std::move(columns), 0);
    std::println("was table created: {}", created);

    auto tbl = catalog::get_table(eng, "hello");
    assert(tbl);

    tbl->insert({42, true, "hello"});
    tbl->insert({43, false, "goodbye"});
    tbl->insert({60, true, "foo"});
    tbl->insert({63, true, "bar"});

    auto cursor = tbl->cursor();

    while (auto row = cursor.next())
        std::println("{}", *row);

    return 0;
}
