cmake_minimum_required(VERSION 3.11)
project(koakuma LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED 23)
set(CMAKE_CXX_STANDARD_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


add_executable(koakuma
  src/buffer_manager.cpp
  src/cross.cpp
  src/pack.cpp
  src/file_manager.cpp
  src/main.cpp
  src/parser/ast.cpp
  src/parser/lexer.cpp
  src/parser/parser.cpp
  src/parser/peekable_lexer.cpp
)

target_include_directories(koakuma
  PRIVATE external
  PRIVATE include
)

target_compile_options(koakuma PRIVATE -Wall -Wextra -Werror)

include(GNUInstallDirs)

install(TARGETS koakuma
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
