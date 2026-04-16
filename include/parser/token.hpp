#ifndef TOKEN_HPP
#define TOKEN_HPP

#include <format>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include "magic_enum/magic_enum.hpp"
#include "types.hpp"

enum class Keyword : u8 {
    All,
    // Analyze,
    And,
    As,
    Between,
    By,
    // Commit,
    Create,
    // Database,
    Delete,
    Distinct,
    Drop,
    Explain,
    False,
    File,
    // Foreign,
    From,
    Group,
    In,
    Index,
    Insert,
    Into,
    Join,
    K,
    Key,
    Not,
    Null,
    On,
    Or,
    Order,
    Point,
    Primary,
    Radius,
    // Rollback,
    Select,
    Set,
    // Start,
    Table,
    // Transaction,
    True,
    Unique,
    Update,
    Values,
    Where,
};

enum class Symbol : u8 {
    Asterisk,
    Comma,
    Div,
    Eq,
    Geq,
    Gt,
    LParen,
    Leq,
    Lt,
    Neq,
    Period,
    Plus,
    RParen,
    SemiColon,
    Sub,
};

enum class DataType : u8 {
    Bool,
    Date,
    Int,
    Real,
    Text,
    Uuid,
    Varchar,
};

struct Number {
    f64 value;
};

struct Literal {
    std::string value;
};

struct Identifier {
    std::string value;
};

struct Eof {};

class Token {
public:
    template<typename T>
    explicit Token(T value)
        : variant{std::move(value)} {}

    template<typename T>
    [[nodiscard]] bool is() const {
        return std::holds_alternative<T>(this->variant);
    }

    template<typename T>
    T& get() {
        return std::get<T>(this->variant);
    }

    template<typename T>
    const T& get() const {
        return std::get<T>(this->variant);
    }

    template<typename T>
    T* get_if() {
        return std::get_if<T>(&this->variant);
    }

    template<typename T>
    const T* get_if() const {
        return std::get_if<T>(&this->variant);
    }

    template<typename F>
    decltype(auto) visit(F&& f) {
        return std::visit(std::forward<F>(f), this->variant);
    }

    template<typename F>
    decltype(auto) visit(F&& f) const {
        return std::visit(std::forward<F>(f), this->variant);
    }

private:
    using token_type = std::variant<Keyword, Symbol, DataType, Number, Literal, Identifier, Eof>;
    token_type variant;
    /* pos, line, ... */
};

template<typename E>
    requires std::is_same_v<E, Keyword> || std::is_same_v<E, Symbol> || std::is_same_v<E, DataType>
struct std::formatter<E, char> {
    constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    auto format(E value, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", magic_enum::enum_name(value));
    }
};

template<>
struct std::formatter<Number, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(Number number, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Number{{{}}}", number.value);
    }
};

template<>
struct std::formatter<Literal, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(Literal literal, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Literal{{{}}}", literal.value);
    }
};

template<>
struct std::formatter<Identifier, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(Identifier identifier, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Identifier{{{}}}", identifier.value);
    }
};

template<>
struct std::formatter<Eof, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format([[maybe_unused]] Eof eof, std::format_context& ctx) {
        return std::format_to(ctx.out(), "Eof");
    }
};

template<>
struct std::formatter<Token, char> {
    static constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    static auto format(Token tok, std::format_context& ctx) {
        return tok.visit([&](auto&& value) { return std::format_to(ctx.out(), "{}", value); });
    }
};

#endif
