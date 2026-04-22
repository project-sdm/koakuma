#ifndef PEEKABLE_LEXER_HPP
#define PEEKABLE_LEXER_HPP

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include "lexer.hpp"
#include "token.hpp"

namespace parser {
    class PeekableLexer {
    public:
        explicit PeekableLexer(std::string);

        std::expected<std::reference_wrapper<Token>, CompileError> peek();
        std::expected<Token, CompileError> next();

    private:
        Lexer lexer;
        std::optional<Token> buf = std::nullopt;
    };
}  // namespace parser

#endif
