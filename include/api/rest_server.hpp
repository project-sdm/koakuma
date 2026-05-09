#ifndef API_REST_SERVER_HPP
#define API_REST_SERVER_HPP

#include <filesystem>
#include <string>
#include "catalog.hpp"
#include "engine/engine.hpp"
#include "httplib/httplib.h"
#include "query_executor.hpp"
#include "types.hpp"

namespace api {

    struct ServerConfig {
        std::string host;
        u16 port;
        std::filesystem::path data_path;
    };

    class RestServer {
    public:
        explicit RestServer(const ServerConfig& cfg = {});

        [[nodiscard]] int run();

    private:
        ServerConfig config;
        Engine eng;
        QueryExecutor executor;
        catalog::Catalog catalog;
        httplib::Server server;

        void setup_routes();
        void handle_query(const httplib::Request& req, httplib::Response& res);
    };

}  // namespace api

#endif
