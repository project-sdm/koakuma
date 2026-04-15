#include "error.hpp"
#include "lexer.hpp"
#include "magic_enum/magic_enum.hpp"
#include "token.hpp"
#include <algorithm>
#include <cstdlib>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace {

  std::expected<Token, CompileError> gen_quoted_identifier(std::string_view lexeme)
  {
    lexeme.remove_prefix(1);
    lexeme.remove_suffix(1);

    if (lexeme.size() == 0)
      return std::unexpected{LexicalError::EmptyQuotedIden};

    std::string out;
    out.reserve(lexeme.size());

    for (std::size_t i = 0; i < lexeme.size(); ++i)
      out.push_back(lexeme[i]);

    return QuotedIdentifier{std::move(out)};
  }

  std::expected<Token, CompileError> gen_escaped_literal(std::string_view lexeme)
  {
    lexeme.remove_prefix(1);
    lexeme.remove_suffix(1);

    std::string out;
    out.reserve(lexeme.size());

    for (std::size_t i = 0; i < lexeme.size(); ++i) {
      if (lexeme[i] != '\\') {
        out.push_back(lexeme[i]);
        continue;
      }

      char next = lexeme[++i];
      switch (next) {
      case 'n':  out.push_back('\n'); break;
      case 'r':  out.push_back('\r'); break;
      case 't':  out.push_back('\t'); break;
      case '\'': out.push_back('\''); break;
      case '\\': out.push_back('\\'); break;
      default:
        return std::unexpected{LexicalError::UnknownEscape};
      }
    }

    return Literal{std::move(out)};
  }

  const std::unordered_map<std::string, Keyword> keyword_table = []{
    std::unordered_map<std::string, Keyword> out;

    for (Keyword keyword : magic_enum::enum_values<Keyword>()) {
      std::string name{magic_enum::enum_name(keyword)};
      std::ranges::transform(name, name.begin(), ::tolower);

      out.emplace(std::move(name), keyword);
    }

    return out;
  }();

  const std::unordered_map<std::string, DataType> type_table = []{
    std::unordered_map<std::string, DataType> out;

    for (DataType type : magic_enum::enum_values<DataType>()) {
      std::string name{magic_enum::enum_name(type)};
      std::ranges::transform(name, name.begin(), ::tolower);

      out.emplace(std::move(name), type);
    }

    return out;
  }();

  using state_t = i16;
  using Row = std::array<state_t, 256>;

  constexpr std::size_t N_STATES = 25;
  const std::array<Row, N_STATES> transition_table = []{
    std::array<Row, N_STATES> out;
    for (std::size_t i = 0; i < N_STATES; ++i) {
      out[i].fill(-1);
    }

    auto add_range = [&](state_t from, state_t to, char a, char b) {
      for (unsigned char c = a; c <= b; ++c)
        out[from][c] = to;
    };

    auto add_alpha = [&](state_t from, state_t to) {
      add_range(from, to, 'a', 'z');
      add_range(from, to, 'A', 'Z');
    };
    auto add_digit = [&](state_t from, state_t to) {
      add_range(from, to, '0', '9');
    };

    out[0]['*'] = 1;
    out[0][','] = 2;
    out[0]['/'] = 3;
    out[0]['='] = 4;

    out[0]['>'] = 5;
    out[5]['='] = 6;

    out[0]['<'] = 7;
    out[7]['='] = 8;
    out[7]['>'] = 9;

    out[0]['('] = 10;
    out[0][')'] = 11;

    out[0]['.'] = 12;
    out[0]['+'] = 13;
    out[0][';'] = 14;
    out[0]['-'] = 15;

    add_digit(0, 16);
    add_digit(16, 16);
    out[16]['.'] = 17;
    add_digit(17, 18);
    add_digit(18, 18);

    add_alpha(0, 19);
    out[0]['_']  = 19;
    add_alpha(19, 19);
    add_digit(19, 19);
    out[19]['_'] = 19;

    out[0]['"']  = 20;
    add_alpha(20, 20);
    add_digit(20, 20);
    out[20]['_'] = 20;
    out[20]['"'] = 21;

    out[0]['\''] = 22;
    for (std::size_t i = 0; i < 256; ++i) {
      unsigned char c = static_cast<unsigned char>(i);
      if (c != '\'' && c != '\\')
        out[22][c] = 22;
    }
    out[22]['\\'] = 23;
    for (std::size_t i = 0; i < 256; ++i) {
      unsigned char c = static_cast<unsigned char>(i);
      out[23][c] = 22;
    }
    out[22]['\''] = 24;

    return out;
  }();

  bool is_ignorable(char chr)
  {
    return chr == ' '  ||
           chr == '\n' ||
           chr == '\t' ||
           chr == '\r';
  }

  bool is_accepting(state_t state)
  {
    return state == 1  ||
           state == 2  ||
           state == 3  ||
           state == 4  ||
           state == 5  ||
           state == 6  ||
           state == 7  ||
           state == 8  ||
           state == 9  ||
           state == 10 ||
           state == 11 ||
           state == 12 ||
           state == 13 ||
           state == 14 ||
           state == 15 ||
           state == 16 ||
           state == 18 ||
           state == 19 ||
           state == 21 ||
           state == 24;
  }

  std::expected<Token, CompileError> tokenize(std::string lexeme, state_t state)
  {
    switch (state) {
    case 1:
      return Symbol::Asterisk;
    case 2:
      return Symbol::Comma;
    case 3:
      return Symbol::Div;
    case 4:
      return Symbol::Eq;
    case 5:
      return Symbol::Gt;
    case 6:
      return Symbol::Geq;
    case 7:
      return Symbol::Lt;
    case 8:
      return Symbol::Leq;
    case 9:
      return Symbol::Neq;
    case 10:
      return Symbol::LParen;
    case 11:
      return Symbol::RParen;
    case 12:
      return Symbol::Period;
    case 13:
      return Symbol::Plus;
    case 14:
      return Symbol::SemiColon;
    case 15:
      return Symbol::Sub;
    case 16:
      return Number{std::stod(lexeme)};
    case 18:
      return Number{std::stod(lexeme)};
    case 19:
      std::ranges::transform(lexeme, lexeme.begin(), ::tolower);
      if (keyword_table.contains(lexeme))
        return keyword_table.at(lexeme);
      if (type_table.contains(lexeme))
        return type_table.at(lexeme);

      return Identifier{std::move(lexeme)};
    case 21:
      return gen_quoted_identifier(lexeme);
    case 24:
      return gen_escaped_literal(lexeme);
    }
    std::unreachable();
  }

  CompileError unwrap_error(state_t state)
  {
    switch (state) {
    case 20:
    case 22:
    case 23:
      return LexicalError::UnexpectedEof;
    default:
      return LexicalError::UnknownToken;
    }
  }

} // namespace

Lexer::Lexer(std::string source)
: source{std::move(source)}
{}

std::expected<Token, CompileError> Lexer::next()
{
  while (auto opt = peek()) {
    if (!is_ignorable(*opt))
      break;

    // TODO: re-add comment support

    consume();
  }

  if (!peek())
    return Eof{};

  std::optional<state_t> accepting_state = std::nullopt;
  std::size_t start = pos, last = pos;
  state_t state = 0;

  while (auto opt = peek()) {
    unsigned char chr = static_cast<unsigned char>(*opt);
    state_t next = transition_table.at(state)[chr];
    if (next == -1)
      break;

    state = next;
    consume();

    if (is_accepting(state)) {
      accepting_state = state;
      last = pos;
    }
  }

  if (state == 0)
    consume();

  if (!accepting_state.has_value())
    return std::unexpected{unwrap_error(state)};

  pos = last;

  std::string lexeme = source.substr(start, last - start);
  return tokenize(std::move(lexeme), *accepting_state);
}

std::optional<char> Lexer::peek()
{
  if (pos >= source.size())
    return std::nullopt;

  return source[pos];
}

void Lexer::consume()
{
  ++pos;
}
