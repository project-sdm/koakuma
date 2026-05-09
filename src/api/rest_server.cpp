#include "api/rest_server.hpp"
#include <chrono>
#include <exception>
#include <expected>
#include <format>
#include <print>
#include <string>
#include <variant>
#include <vector>
#include "catalog.hpp"
#include "engine/engine.hpp"
#include "file/seq_file.hpp"
#include "httplib/httplib.h"
#include "json/json.hpp"
#include "magic_enum/magic_enum.hpp"
#include "parser/parser.hpp"
#include "query_executor.hpp"

using nlohmann::json;

namespace std {

    // serialize std::variant as whatever its active value is
    template<typename... Types>
    void to_json(json& j, const variant<Types...>& var) {  // NOLINT(misc-use-internal-linkage)
        std::visit([&](const auto& val) { j = val; }, var);
    }

}  // namespace std

void to_json(json& j, const Column& col) {  // NOLINT(misc-use-internal-linkage)
    j = json{
        {"name",                        col.name},
        {"type", magic_enum::enum_name(col.type)},
    };
}

struct QueryResult {
    std::vector<Column> columns;
    std::vector<Row> rows;
};

void to_json(json& j,  // NOLINT(misc-use-internal-linkage)
             const QueryResult& result) {
    j = json{
        {"columns", result.columns},
        {   "rows",    result.rows},
    };
}

namespace {

    class AccumulatorSink : public QueryExecutor::RowSink {
    public:
        void on_columns(const std::vector<Column>& columns) override {
            results.emplace_back(columns);
        }

        void on_row(const Row& row) override {
            results.back().rows.push_back(row);
        }

        [[nodiscard]] const std::vector<QueryResult>& get_results() const {
            return results;
        }

        ~AccumulatorSink() override = default;

    private:
        std::vector<QueryResult> results;
    };

}  // namespace

namespace api {

    void RestServer::setup_routes() {
        server.Post("/", [](const httplib::Request&, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");

            json j = {
                {"message", "Hello, world!"}
            };

            res.set_content(j.dump(), "application/json");
        });

        server.Post("/query", [this](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            handle_query(req, res);
        });
    }

    void RestServer::handle_query(const httplib::Request& req, httplib::Response& res) {
        using clock = std::chrono::steady_clock;

        using httplib::StatusCode;

        parser::Parser parser{req.body};
        auto parsed = parser.source_file();

        if (!parsed) {
            res.status = StatusCode::BadRequest_400;
            json j = {
                {"detail", std::format("{}", parsed.error())}
            };
            res.set_content(j.dump(), "application/json");
            return;
        }

        u64 reads_before = eng.file_mgr.get_read_counter();
        u64 writes_before = eng.file_mgr.get_write_counter();

        AccumulatorSink sink{};

        std::expected<void, ExecutionError> exec_res;

        u64 time_ms = 0;

        try {
            auto start = clock::now();
            exec_res = executor.exec(catalog, *parsed, sink);
            auto end = clock::now();

            time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        } catch (const std::exception& ex) {
            std::println("Unexpected exception: {}", ex.what());
            res.status = StatusCode::InternalServerError_500;

            json j = {
                {"detail", "Internal server error."}
            };
            res.set_content(j.dump(), "application/json");
            return;
        }

        if (!exec_res) {
            res.status = StatusCode::BadRequest_400;

            json j = {
                {"detail", std::format("{}", exec_res.error())}
            };
            res.set_content(j.dump(), "application/json");
            return;
        }

        u64 disk_reads = eng.file_mgr.get_read_counter() - reads_before;
        u64 disk_writes = eng.file_mgr.get_write_counter() - writes_before;

        json j = {
            {"results", sink.get_results()},
            {"time_ms",            time_ms},
            {  "reads",         disk_reads},
            { "writes",        disk_writes},
        };

        res.set_content(j.dump(), "application/json");
    }

    RestServer::RestServer(const ServerConfig& cfg)
        : config{cfg},
          executor{eng},
          catalog{cfg.data_path} {
        setup_routes();
    }

    int RestServer::run() {
        std::println(stderr, "Server listening on {}:{}", config.host, config.port);

        if (!server.listen(config.host, config.port)) {
            std::println(stderr, "Failed to bind server to {}:{}", config.host, config.port);
            return 1;
        }

        return 0;
    }

}  // namespace api
