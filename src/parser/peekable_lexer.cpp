#include "parser/peekable_lexer.hpp"
#include <expected>
#include <functional>
#include <string>
#include <utility>
#include "parser/error.hpp"
#include "parser/lexer.hpp"
#include "parser/token.hpp"
#include "util.hpp"

namespace parser {
    PeekableLexer::PeekableLexer(std::string source)
        : lexer{std::move(source)} {}

    std::expected<std::reference_wrapper<Token>, CompileError> PeekableLexer::peek() {
        if (!buf.has_value())
            buf = TRY(lexer.next());

        return buf.value();
    }

    std::expected<Token, CompileError> PeekableLexer::next() {
        if (buf)
            return std::exchange(buf, std::nullopt).value();

        return lexer.next();
    }
}  // namespace parser
