#include <cassert>
#include <format>
#include <print>
#include <string>
#include "api/rest_server.hpp"
#include "util.hpp"

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::println(R"( _  __           _                          )");
    std::println(R"(| |/ /___   __ _| | ___   _ _ __ ___   __ _ )");
    std::println(R"(| ' // _ \ / _` | |/ / | | | '_ ` _ \ / _` |)");
    std::println(R"(| . \ (_) | (_| |   <| |_| | | | | | | (_| |)");
    std::println(R"(|_|\_\___/ \__,_|_|\_\\__,_|_| |_| |_|\__,_|)");
    std::println();
    std::println("Page size: {}", PAGE_SIZE);

    api::ServerConfig config{
        .host = util::getenv_or("KOAKUMA_HOST", "0.0.0.0"),
        .port = static_cast<u16>(std::stoul(util::getenv_or("KOAKUMA_PORT", "8080"))),
        .data_path = util::getenv_or("KOAKUMA_DATA_PATH", ".data"),
    };
    api::RestServer server{config};
    return server.run();
}
