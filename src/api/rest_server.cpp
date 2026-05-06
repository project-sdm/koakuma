#include "api/rest_server.hpp"
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <format>
#include <print>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include "catalog.hpp"
#include "engine/engine.hpp"
#include "httplib/httplib.h"
#include "parser/parser.hpp"
#include "query_executor.hpp"

namespace {

    constexpr u16 HTTP_OK = 200;
    constexpr u16 HTTP_BAD_REQUEST = 400;
    constexpr u16 HTTP_INTERNAL_SERVER_ERROR = 500;
    constexpr std::string_view ERROR_CODE_VALIDATION = "VALIDATION_ERROR";
    constexpr std::string_view ERROR_CODE_PARSE = "PARSE_ERROR";
    constexpr std::string_view ERROR_CODE_EXECUTION = "EXECUTION_ERROR";
    constexpr std::string_view MESSAGE_EMPTY_QUERY =
        "empty query: send SQL in request body or sql parameter";
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
            default:
                std::unreachable();
        }
    }

    std::atomic<u64> g_request_counter = 0;

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
        return std::format(R"({{"status":"success","requestId":"req-{}","data":{}}})", request_id,
                           data_json);
    }

    std::string make_error_json(u32 request_id,
                                std::string_view code,
                                std::string_view message,
                                std::string_view detail = {}) {
        if (detail.empty()) {
            return std::format(
                "{{\"status\":\"error\",\"requestId\":\"req-{}\",\"error\":{{\"code\":\"{}\","
                "\"message\":\"{}\"}}}}",
                request_id, json_escape(code), json_escape(message));
        }

        return std::format(
            "{{\"status\":\"error\",\"requestId\":\"req-{}\",\"error\":{{\"code\":\"{}\","
            "\"message\":\"{}\",\"detail\":\"{}\"}}}}",
            request_id, json_escape(code), json_escape(message), json_escape(detail));
    }

    void respond_json(httplib::Response& res, int status_code, const std::string& body) {
        res.status = status_code;
        res.set_content(body, "application/json");
    }

    std::string strip_trailing_whitespace(std::string value) {
        while (!value.empty() && std::isspace(value.back()) != 0)
            value.pop_back();

        return value;
    }

    std::string read_query_from_request(const httplib::Request& req) {
        if (!req.body.empty())
            return strip_trailing_whitespace(req.body);

        if (req.has_param("sql"))
            return strip_trailing_whitespace(req.get_param_value("sql"));

        if (req.has_param("query"))
            return strip_trailing_whitespace(req.get_param_value("query"));

        return {};
    }

    class JsonRowSink final : public QueryExecutor::RowSink {
    public:
        JsonRowSink(std::string& columns_json,
                    std::string& rows_json,
                    bool& first_column,
                    bool& first_row)
            : columns_json{columns_json},
              rows_json{rows_json},
              first_column{first_column},
              first_row{first_row} {}

        void on_columns(const std::vector<Column>& columns) override {
            columns_json = "[";
            rows_json = "[";
            first_column = true;
            first_row = true;

            for (const auto& column : columns) {
                if (!first_column)
                    columns_json += ',';

                columns_json +=
                    std::format(R"({{"name":"{}","type":{}}})", json_escape(column.name),
                                column_type_to_json(column.type));
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
              executor{eng},
              catalog{cfg.data_path} {
            setup_routes();
        }

        void setup_routes() {
            server.Post("/", [](const httplib::Request&, httplib::Response& res) {
                respond_json(res, HTTP_OK, R"({"message":"Hello, world!"})");
            });

            server.Post("/query", [this](const httplib::Request& req, httplib::Response& res) {
                handle_query(req, res);
            });
        }

        void handle_query(const httplib::Request& req, httplib::Response& res) {
            auto request_id = next_request_id();
            auto sql = read_query_from_request(req);

            if (sql.empty()) {
                respond_json(
                    res, HTTP_BAD_REQUEST,
                    make_error_json(request_id, ERROR_CODE_VALIDATION, MESSAGE_EMPTY_QUERY));
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
                respond_json(
                    res, HTTP_BAD_REQUEST,
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
            const u64 reads_before = eng.file_mgr.get_read_counter();
            const u64 writes_before = eng.file_mgr.get_write_counter();

            JsonRowSink sink{columns_json, rows_json, first_column, first_row};

            try {
                accepted_statements = executor.exec(catalog, *parsed, sink);
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

            const u64 disk_reads = eng.file_mgr.get_read_counter() - reads_before;
            const u64 disk_writes = eng.file_mgr.get_write_counter() - writes_before;

            columns_json += "]";
            rows_json += "]";
            std::string data = std::format(
                "{{\"acceptedStatements\":{},\"timingMs\":{{\"parse\":{},\"exec\":{}}},\"diskIO\":{"
                "{\"reads\":{},\"writes\":{}}},\"columns\":{},\"rows\":{}}}",
                accepted_statements, parse_ms, exec_ms, disk_reads, disk_writes, columns_json,
                rows_json);
            respond_json(res, HTTP_OK, make_success_json(request_id, data));
        }

        int run() {
            std::println("Server listening on {}:{}", config.host, config.port);

            if (!server.listen(config.host, config.port)) {
                std::println(stderr, "Failed to bind server to {}:{}", config.host, config.port);
                return 1;
            }

            return 0;
        }

        ServerConfig config;
        Engine eng;
        QueryExecutor executor;
        catalog::Catalog catalog;
        httplib::Server server;
    };

    RestServer::RestServer(ServerConfig config)
        : impl{std::make_unique<Impl>(std::move(config))} {}

    int RestServer::run() {
        return impl->run();
    }

    int run_rest_server(const ServerConfig& config) {
        RestServer server{config};
        return server.run();
    }

}  // namespace api
