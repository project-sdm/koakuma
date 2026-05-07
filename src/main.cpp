#include <cassert>
#include <format>
#include <print>
#include "catalog.hpp"
#include "engine/file_manager.hpp"
#include "parser/parser.hpp"
#include "query_executor.hpp"
#include "util.hpp"

class PrintSink : public QueryExecutor::RowSink {
public:
    void on_columns(const std::vector<Column>& columns) override {
        for (const auto& col : columns)
            std::print("{} ", col.name);

        std::println();
    }

    void on_row(const Row& row) override {
        std::println("{}", row);
    }

    ~PrintSink() override = default;
};

int main() {
    std::println(R"( _  __           _                          )");
    std::println(R"(| |/ /___   __ _| | ___   _ _ __ ___   __ _ )");
    std::println(R"(| ' // _ \ / _` | |/ / | | | '_ ` _ \ / _` |)");
    std::println(R"(| . \ (_) | (_| |   <| |_| | | | | | | (_| |)");
    std::println(R"(|_|\_\___/ \__,_|_|\_\\__,_|_| |_| |_|\__,_|)");
    std::println();
    std::println("Page size: {}", PAGE_SIZE);
    std::println();

    std::ifstream file{"./data/test.sql"};
    if (!file.is_open()) {
        std::println("{}", std::make_error_code(std::errc(errno)).message());
        return EXIT_FAILURE;
    }

    std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    file.close();

    parser::Parser parser{source};
    auto source_res = parser.source_file();

    if (!source_res) {
        std::println("{}", source_res.error());
        return 1;
    }

    std::println("{}", *source_res);

    Engine eng{};

    QueryExecutor executor{eng};

    catalog::Catalog catalog{util::getenv_or("KOAKUMA_DATA_PATH", "./.data")};

    PrintSink sink{};
    auto res = executor.exec(catalog, *source_res, sink);
    if (!res)
        std::println("error: {}", res.error());

    return 0;
}
