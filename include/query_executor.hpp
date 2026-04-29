#ifndef QUERY_EXECUTOR_HPP
#define QUERY_EXECUTOR_HPP

#include "engine/engine.hpp"
#include "parser/ast.hpp"

class QueryExecutor {
public:
    explicit QueryExecutor(Engine& engine);

    void exec(const parser::SourceFile& source_file);

private:
    struct Executor {
        Engine& engine;

        void operator()(const parser::CreateStatement& stmt) const;
        void operator()(const parser::SelectStatement& stmt) const;
        void operator()(const parser::InsertStatement& stmt) const;
        void operator()(const parser::DeleteStatement& stmt) const;
    };

    Engine& engine;
};

#endif
