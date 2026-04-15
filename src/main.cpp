#include "lexer.hpp"
#include "parser.hpp"
#include <system_error>
#include <fstream>
#include <print>

int main() {
  std::ifstream file{"./data/test.sql"};
  if (!file.is_open()) {
    std::println("{}", std::make_error_code(std::errc(errno)).message());
    return EXIT_FAILURE;
  }

  std::string source{
    std::istreambuf_iterator<char>(file),
    std::istreambuf_iterator<char>()
  };
  file.close();

  Parser parser{source};
  auto res = parser.source_file();
  if (!res.has_value())
    for (auto err : res.error())
      std::println("{}", err);
  else
    std::println("{}", res.value());
}
