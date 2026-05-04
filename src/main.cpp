#include <cassert>
#include <format>
#include <print>
#include "engine/engine.hpp"
#include "engine/file_manager.hpp"
#include "index/btree.hpp"

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

    auto fid = eng.file_mgr.open_create("hello.index.bin");
    BTreeIndex index{eng, fid};

    index.init();

    for (int i = 0; i < 20; ++i)
        index.add(4 * i, Rid{static_cast<u32>(i), static_cast<u32>(i + 1)});

    index.ugly_print();

    auto cursor = index.range_search(21, 100);
    while (auto rid = cursor.next()) {
        std::println("rid: {},{}", rid->pnum, rid->slot_idx);
    }

    return 0;

    index.remove(0);
    index.remove(32);
    index.remove(8);

    // index.add(5, Rid{static_cast<u32>(4), static_cast<u32>(8)});
    // index.add(6, Rid{static_cast<u32>(6), static_cast<u32>(7)});

    // index.add(37, Rid{static_cast<u32>(4), static_cast<u32>(8)});
    // index.add(38, Rid{static_cast<u32>(6), static_cast<u32>(7)});

    index.ugly_print();
    std::println();

    std::println("removing 24");
    index.remove(24);

    index.ugly_print();
    std::println();

    std::println("removing 16");
    index.remove(16);

    index.ugly_print();

    // index.insert(12, Rid{123, 345});

    eng.file_mgr.close(fid);

    return 0;
}
