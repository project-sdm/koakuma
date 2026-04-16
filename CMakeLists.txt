cmake_minimum_required(VERSION 3.11)
project(koakuma LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED 23)
set(CMAKE_CXX_STANDARD_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


add_executable(koakuma
  src/ast.cpp
  src/lexer.cpp
  src/main.cpp
  src/parser.cpp
  src/peekable.cpp
)

target_include_directories(koakuma
  PRIVATE external
  PRIVATE include
)

target_compile_options(koakuma PRIVATE -Wall -Wextra -Werror)
