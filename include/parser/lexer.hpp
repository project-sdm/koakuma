#ifndef LEXER_HPP
#define LEXER_HPP

#include <expected>
#include <optional>
#include <string>
#include "error.hpp"
#include "token.hpp"

class Lexer {
public:
    explicit Lexer(std::string);

    std::expected<Token, CompileError> next();

private:
    std::optional<char> peek();
    void consume();

    std::string source;
    std::size_t pos = 0;
};

#endif
