#include "api/rest_server.hpp"
#include <chrono>
#include <exception>
#include <expected>
#include <format>
#include <optional>
#include <print>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "catalog.hpp"
#include "engine/engine.hpp"
#include "file/common.hpp"
#include "httplib/httplib.h"
#include "index/rtree.hpp"
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

    template<typename T>
    void to_json(json& j, const optional<T>& var) {  // NOLINT(misc-use-internal-linkage)
        if (var.has_value())
            j = var.value();
        else
            j = nullptr;
    }

}  // namespace std

void to_json(json& j, const Rect<2>& rect) {  // NOLINT(misc-use-internal-linkage)
    j = json{
        {"min", rect.min},
        {"max", rect.max},
    };
}

void to_json(json& j, const Column& col) {  // NOLINT(misc-use-internal-linkage)
    j = json{
        {"name",                        col.name},
        {"type", magic_enum::enum_name(col.type)},
    };
}

struct QueryResult {
    struct Table {
        std::vector<Column> columns;
        std::vector<Row> rows;

        explicit Table(std::vector<Column> columns)
            : columns{std::move(columns)} {}
    };

    struct Plane {
        std::vector<std::pair<u64, Rect<2>>> rects;
    };

    std::optional<Table> table;
    std::optional<Plane> plane;
    std::vector<std::string> warnings;
};

void to_json(json& j, const QueryResult::Table& table) {  // NOLINT(misc-use-internal-linkage)
    j = json{
        {"columns", table.columns},
        {   "rows",    table.rows},
    };
}

void to_json(json& j, const QueryResult::Plane& plane) {  // NOLINT(misc-use-internal-linkage)
    j = json{
        {"rects", plane.rects},
    };
}

void to_json(json& j, const QueryResult& result) {  // NOLINT(misc-use-internal-linkage)
    j = json{
        {   "table",    result.table},
        {   "plane",    result.plane},
        {"warnings", result.warnings}
    };
}

namespace {

    class AccumulatorSink : public QueryExecutor::RowSink {
    public:
        void on_begin() override {
            results.emplace_back();
        }

        void on_table(const std::vector<Column>& columns) override {
            results.back().table = QueryResult::Table{columns};
        }

        void on_row(const Row& row) override {
            results.back().table->rows.push_back(row);
        }

        void on_plane() override {
            results.back().plane = QueryResult::Plane{};
        }

        void on_rect(u64 level, const Rect<2>& rect) override {
            results.back().plane->rects.emplace_back(level, rect);
        }

        void on_warning(const std::string& warning) override {
            results.back().warnings.push_back(warning);
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
        server.Get("/", [](const httplib::Request&, httplib::Response& res) {
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
        std::println("Server listening on {}:{}", config.host, config.port);

        if (!server.listen(config.host, config.port)) {
            std::println("Failed to bind server to {}:{}", config.host, config.port);
            return 1;
        }

        return 0;
    }

}  // namespace api
