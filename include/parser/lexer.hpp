#ifndef LEXER_HPP
#define LEXER_HPP

#include <expected>
#include <optional>
#include <string>
#include "error.hpp"
#include "token.hpp"
#include "util.hpp"

namespace parser {

    class Lexer {
    public:
        using value_type = std::expected<Token, CompileError>;

        explicit Lexer(std::string);

        std::optional<value_type> next();

    private:
        std::optional<char> peek();
        void consume();

        std::string source;
        std::size_t pos = 0;
    };

    static_assert(util::iter<Lexer>);

}  // namespace parser

#endif
