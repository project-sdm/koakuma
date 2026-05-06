#ifndef API_REST_SERVER_HPP
#define API_REST_SERVER_HPP

#include <memory>
#include <string>
#include <string_view>

#include "types.hpp"

namespace api {

    enum class ErrorCode : u8 {
        ValidationError,
        ParseError,
        ExecutionError,
        InternalError,
    };

    struct ServerConfig {
        std::string host;
        u16 port;
        std::string data_path;
    };

    struct ResponseTiming {
        u32 parse_ms = 0;
        u32 exec_ms = 0;
    };

    struct ResponseError {
        ErrorCode code = ErrorCode::InternalError;
        std::string message;
        std::string detail;
    };

    struct ResponseData {
        u32 accepted_statements = 0;
        ResponseTiming timing;
    };

    struct ApiResponse {
        bool ok = false;
        u32 request_id = 0;
        std::string_view data_json;
        ResponseError error;
    };

    class RestServer {
    public:
        explicit RestServer(ServerConfig config = {});

        [[nodiscard]] int run();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    int run_rest_server(const ServerConfig& config = {});

}  // namespace api

#endif
