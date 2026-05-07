#ifndef QUERY_EXECUTOR_HPP
#define QUERY_EXECUTOR_HPP

#include "catalog.hpp"
#include "engine/engine.hpp"
#include "parser/ast.hpp"
#include "seq_file.hpp"

class QueryExecutor {
public:
    struct RowSink {
        virtual void on_columns(const std::vector<Column>& columns) = 0;
        virtual void on_row(const Row& row) = 0;
        virtual ~RowSink() = default;
    };

    explicit QueryExecutor(Engine& engine);

    u32 exec(const catalog::Catalog& catalog, const parser::SourceFile& source_file, RowSink& sink);

private:
    struct Executor {
        Engine& eng;
        const catalog::Catalog& catalog;
        RowSink& sink;

        void operator()(const parser::CreateStatement& stmt) const;
        void operator()(const parser::SelectStatement& stmt);
        void operator()(const parser::InsertStatement& stmt) const;
        void operator()(const parser::DeleteStatement& stmt) const;
    };

    Engine& engine;
};

#endif
