#include "api/rest_server.hpp"
#include <atomic>
#include <cctype>
#include <chrono>
#include <exception>
#include <format>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include "api/frontend.hpp"
#include "engine/engine.hpp"
#include "httplib/httplib.h"
#include "parser/parser.hpp"
#include "query_executor.hpp"

namespace {

constexpr std::string_view SERVICE_NAME = "koakuma";
constexpr std::string_view ROUTE_ROOT = "/";
constexpr std::string_view ROUTE_HEALTH = "/health";
constexpr std::string_view ROUTE_QUERY = "/query";
constexpr std::string_view CONTENT_TYPE_HTML = "text/html";
constexpr std::string_view CONTENT_TYPE_JSON = "application/json";
constexpr u16 HTTP_OK = 200;
constexpr u16 HTTP_BAD_REQUEST = 400;
constexpr u16 HTTP_PAYLOAD_TOO_LARGE = 413;
constexpr u16 HTTP_INTERNAL_SERVER_ERROR = 500;
constexpr std::string_view ERROR_CODE_VALIDATION = "VALIDATION_ERROR";
constexpr std::string_view ERROR_CODE_PARSE = "PARSE_ERROR";
constexpr std::string_view ERROR_CODE_EXECUTION = "EXECUTION_ERROR";
constexpr std::string_view MESSAGE_EMPTY_QUERY =
    "empty query: send SQL in request body or sql parameter";
constexpr std::string_view MESSAGE_QUERY_TOO_LARGE = "query is too large";
constexpr std::string_view MESSAGE_QUERY_PARSE_FAILED = "query parse failed";
constexpr std::string_view MESSAGE_QUERY_EXECUTION_FAILED = "query execution failed";

std::string json_escape(std::string_view input);

std::string column_type_to_json(ColumnType type) {
    switch (type) {
        case ColumnType::INT:
            return "\"int\"";
        case ColumnType::BOOL:
            return "\"bool\"";
        case ColumnType::FLOAT:
            return "\"float\"";
        case ColumnType::STRING:
            return "\"string\"";
    }

    return "\"unknown\"";
}

std::atomic<u32> g_request_counter = 0;

u32 next_request_id() {
    return 1U + g_request_counter.fetch_add(1, std::memory_order_relaxed);
}

std::string json_escape(std::string_view input) {
    std::string out;
    out.reserve(input.size());

    for (const char c : input) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }

    return out;
}

std::string value_to_json(const Value& value) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return std::format("\"{}\"", json_escape(v));
            } else if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else {
                return std::format("{}", v);
            }
        },
        value);
}

std::string row_to_json(const Row& row) {
    std::string out = "[";
    for (std::size_t i = 0; i < row.size(); ++i) {
        if (i > 0)
            out += ',';
        out += value_to_json(row[i]);
    }
    out += ']';
    return out;
}

std::string make_success_json(u32 request_id, std::string_view data_json) {
    return std::format("{{\"status\":\"success\",\"requestId\":\"req-{}\",\"data\":{}}}",
                       request_id, data_json);
}

std::string make_error_json(u32 request_id, std::string_view code, std::string_view message,
                            std::string_view detail = {}) {
    if (detail.empty()) {
        return std::format(
            "{{\"status\":\"error\",\"requestId\":\"req-{}\",\"error\":{{\"code\":\"{}\",\"message\":\"{}\"}}}}",
            request_id, json_escape(code), json_escape(message));
    }

    return std::format(
        "{{\"status\":\"error\",\"requestId\":\"req-{}\",\"error\":{{\"code\":\"{}\",\"message\":\"{}\",\"detail\":\"{}\"}}}}",
        request_id, json_escape(code), json_escape(message), json_escape(detail));
}

void respond_json(httplib::Response& res, u16 status_code, const std::string& body) {
    res.status = static_cast<int>(status_code);
    res.set_content(body, CONTENT_TYPE_JSON.data());
}

std::string strip_trailing_whitespace(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();

    return value;
}

std::string normalize_sql_query(std::string query) {
    query = strip_trailing_whitespace(std::move(query));

    if (!query.empty() && query.back() != ';')
        query.push_back(';');

    return query;
}

std::string read_query_from_request(const httplib::Request& req) {
    if (!req.body.empty())
        return normalize_sql_query(req.body);

    if (req.has_param("sql"))
        return normalize_sql_query(req.get_param_value("sql"));

    if (req.has_param("query"))
        return normalize_sql_query(req.get_param_value("query"));

    return {};
}

class JsonRowSink final : public QueryExecutor::RowSink {
public:
    JsonRowSink(std::string& columns_json, std::string& rows_json, bool& first_column,
                bool& first_row)
        : columns_json{columns_json},
          rows_json{rows_json},
          first_column{first_column},
          first_row{first_row} {}

    void on_columns(const std::vector<Column>& columns) override {
        for (const auto& column : columns) {
            if (!first_column)
                columns_json += ',';

            columns_json += std::format("{{\"name\":\"{}\",\"type\":{}}}",
                                        json_escape(column.name), column_type_to_json(column.type));
            first_column = false;
        }
    }

    void on_row(const Row& row) override {
        if (!first_row)
            rows_json += ',';
        rows_json += row_to_json(row);
        first_row = false;
    }

private:
    std::string& columns_json;
    std::string& rows_json;
    bool& first_column;
    bool& first_row;
};

}  // namespace

namespace api {

struct RestServer::Impl {
    explicit Impl(ServerConfig cfg)
        : config{std::move(cfg)},
          engine{},
          executor{engine} {
        setup_routes();
    }

    void setup_routes() {
        server.Get(ROUTE_ROOT.data(), [](const httplib::Request&, httplib::Response& res) {
            res.set_content(std::string{FRONTEND_HTML}, CONTENT_TYPE_HTML.data());
        });

        server.Get(ROUTE_HEALTH.data(), [this](const httplib::Request&, httplib::Response& res) {
            const auto request_id = next_request_id();
            respond_json(res, HTTP_OK,
                         make_success_json(request_id,
                                           std::format("{{\"service\":\"{}\",\"state\":\"ok\"}}",
                                                       SERVICE_NAME)));
        });

        server.Post(ROUTE_QUERY.data(), [this](const httplib::Request& req, httplib::Response& res) {
            handle_query(req, res);
        });
    }

    void handle_query(const httplib::Request& req, httplib::Response& res) {
        const auto request_id = next_request_id();
        auto sql = read_query_from_request(req);

        if (sql.empty()) {
            respond_json(res, HTTP_BAD_REQUEST,
                         make_error_json(request_id, ERROR_CODE_VALIDATION, MESSAGE_EMPTY_QUERY));
            return;
        }

        if (sql.size() > MAX_QUERY_BYTES) {
            respond_json(res, HTTP_PAYLOAD_TOO_LARGE,
                         make_error_json(request_id, ERROR_CODE_VALIDATION, MESSAGE_QUERY_TOO_LARGE,
                                         std::format("maxBytes={}", MAX_QUERY_BYTES)));
            return;
        }

        using clock = std::chrono::steady_clock;
        const auto parse_start = clock::now();

        parser::Parser parser{std::move(sql)};
        auto parsed = parser.source_file();
        const auto parse_end = clock::now();

        const auto parse_ms = static_cast<u32>(
            std::chrono::duration_cast<std::chrono::milliseconds>(parse_end - parse_start)
                .count());

        if (!parsed) {
            respond_json(res, HTTP_BAD_REQUEST,
                         make_error_json(request_id, ERROR_CODE_PARSE, MESSAGE_QUERY_PARSE_FAILED,
                                         std::format("{}", parsed.error())));
            return;
        }

        const auto exec_start = clock::now();
        std::string columns_json = "[";
        std::string rows_json = "[";
        bool first_column = true;
        bool first_row = true;
        u32 accepted_statements = 0;
        JsonRowSink sink{columns_json, rows_json, first_column, first_row};
        try {
            accepted_statements = executor.exec(*parsed, sink);
        } catch (const std::exception& ex) {
            respond_json(res, HTTP_INTERNAL_SERVER_ERROR,
                         make_error_json(request_id, ERROR_CODE_EXECUTION,
                                         MESSAGE_QUERY_EXECUTION_FAILED, ex.what()));
            return;
        } catch (...) {
            respond_json(res, HTTP_INTERNAL_SERVER_ERROR,
                         make_error_json(request_id, ERROR_CODE_EXECUTION,
                                         MESSAGE_QUERY_EXECUTION_FAILED, "unknown exception"));
            return;
        }
        const auto exec_end = clock::now();

        const auto exec_ms = static_cast<u32>(
            std::chrono::duration_cast<std::chrono::milliseconds>(exec_end - exec_start)
                .count());

        columns_json += "]";
        rows_json += "]";
        std::string data = std::format(
            "{{\"acceptedStatements\":{},\"timingMs\":{{\"parse\":{},\"exec\":{}}},\"columns\":{},\"rows\":{}}}",
            accepted_statements, parse_ms, exec_ms, columns_json, rows_json);
        respond_json(res, HTTP_OK, make_success_json(request_id, data));
    }

    int run() {
        std::println("{} API listening on http://{}:{}", SERVICE_NAME, config.host, config.port);
        std::fflush(stdout);

        if (!server.listen(config.host, config.port)) {
            std::println("failed to bind {} API server to {}:{}", SERVICE_NAME, config.host,
                         config.port);
            return 1;
        }

        return 0;
    }

    ServerConfig config;
    Engine engine;
    QueryExecutor executor;
    httplib::Server server;
};

RestServer::RestServer(ServerConfig config)
    : impl{std::make_unique<Impl>(std::move(config))} {}

RestServer::~RestServer() = default;
RestServer::RestServer(RestServer&&) noexcept = default;
RestServer& RestServer::operator=(RestServer&&) noexcept = default;

int RestServer::run() {
    return impl->run();
}

int run_rest_server(const ServerConfig& config) {
    RestServer server{config};
    return server.run();
}

}  // namespace api
