#ifndef PEEKABLE_HPP
#define PEEKABLE_HPP

#include <expected>
#include <optional>
#include <string>
#include "lexer.hpp"
#include "token.hpp"

class Peekable {
public:
    explicit Peekable(std::string);

    std::expected<Token, CompileError> peek();
    std::expected<Token, CompileError> next();

private:
    Lexer lexer;
    std::optional<Token> buf = std::nullopt;
};

#endif
