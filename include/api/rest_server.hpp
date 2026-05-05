#ifndef API_REST_SERVER_HPP
#define API_REST_SERVER_HPP

#include <memory>
#include <string>
#include <string_view>

#include "types.hpp"

namespace api {

inline constexpr u32 MAX_QUERY_BYTES = 64 * 1024;

enum class ErrorCode {
    ValidationError,
    ParseError,
    ExecutionError,
    InternalError,
};

struct ServerConfig {
    std::string host = "0.0.0.0";
    u16 port = 8080;
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
    ~RestServer();

    RestServer(const RestServer&) = delete;
    RestServer& operator=(const RestServer&) = delete;
    RestServer(RestServer&&) noexcept;
    RestServer& operator=(RestServer&&) noexcept;

    int run();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

int run_rest_server(const ServerConfig& config = {});

}

#endif
