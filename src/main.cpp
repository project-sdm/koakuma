#include <cassert>
#include <format>
#include <print>
#include <vector>
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
    std::println();

    Engine eng{};

    auto fid = eng.file_mgr.open_create("hello.bin");

    std::vector<Column> columns = {
        {    "id",    ColumnType::INT},
        {  "name", ColumnType::STRING},
        {"active",   ColumnType::BOOL},
    };

    SeqFile seq_file{eng, fid};
    seq_file.init(columns, 0);

    for (int i = 0; i < 20; ++i) {
        int key = 100 - i;
        std::println("inserting {}", key);
        seq_file.add({key, std::format("hello {}", i + 1), (bool)(i % 2)});
    }

    for (auto row : seq_file)
        std::println("{} ", row);

    // BTreeIndex index{eng, fid};

    // index.init();

    // for (int i = 0; i < 32; ++i) {
    //     int key = i;
    //     index.add(key, Rid{static_cast<u32>(i), static_cast<u32>(i + 1)});
    //     index.ugly_print();
    //     std::println();
    // }

    // index.add(1, Rid{0, 0});
    // index.add(2, Rid{0, 0});

    // index.ugly_print();
    // std::println();

    // auto cursor = index.search(0);
    // while (auto rid = cursor.next())
    //     std::println("{} {}", rid->pnum, rid->slot_idx);

    // auto cursor = index.range_search(21, 100);
    // while (auto rid = cursor.next()) {
    //     std::println("rid: {},{}", rid->pnum, rid->slot_idx);
    // }

    // index.remove(0);
    // index.remove(32);
    // index.remove(8);

    // index.add(5, Rid{static_cast<u32>(4), static_cast<u32>(8)});
    // index.add(6, Rid{static_cast<u32>(6), static_cast<u32>(7)});

    // index.add(37, Rid{static_cast<u32>(4), static_cast<u32>(8)});
    // index.add(38, Rid{static_cast<u32>(6), static_cast<u32>(7)});

    // index.ugly_print();
    // std::println();

    // std::println("removing 24");
    // index.remove(24);

    // index.ugly_print();
    // std::println();

    // std::println("removing 16");
    // index.remove(16);

    // index.insert(12, Rid{123, 345});

    eng.file_mgr.close(fid);
    */

    // std::println("starting koakuma REST API server...");
    // return api::run_rest_server();

    return 0;
}
