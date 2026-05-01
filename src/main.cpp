#include <cassert>
#include <cstdlib>
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

    for (int i = 0; i < 100; ++i) {
        std::println();
        int key = std::rand() % 500;
        std::println("inserting {}", key);

        bool result = index.add(key, Rid{static_cast<u32>(2 * i), static_cast<u32>(3 * i)});

        index.ugly_print();
    }

    // index.insert(12, Rid{123, 345});

    eng.file_mgr.close(fid);

    return 0;
}
