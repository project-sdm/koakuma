#include <cassert>
#include <format>
#include <fstream>
#include <iterator>
#include <print>
#include "engine/engine.hpp"
#include "parser/parser.hpp"
#include "query_executor.hpp"

int main() {
    std::println(R"( _  __           _                          )");
    std::println(R"(| |/ /___   __ _| | ___   _ _ __ ___   __ _ )");
    std::println(R"(| ' // _ \ / _` | |/ / | | | '_ ` _ \ / _` |)");
    std::println(R"(| . \ (_) | (_| |   <| |_| | | | | | | (_| |)");
    std::println(R"(|_|\_\___/ \__,_|_|\_\\__,_|_| |_| |_|\__,_|)");
    std::println();
    std::println("Page size: {}", PAGE_SIZE);

    std::ifstream file{"./data/test.sql"};
    std::string source{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};

    parser::Parser parser{source};
    auto result = parser.source_file();

    if (!result.has_value()) {
        std::println("Error: {}", result.error());
    } else {
        Engine eng{};
        QueryExecutor executor{eng};
        executor.exec(*result);
    }
}
