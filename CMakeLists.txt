cmake_minimum_required(VERSION 3.11)
project(koakuma LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED 23)
set(CMAKE_CXX_STANDARD_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(
        -g
        -Og
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )

    add_link_options(
        -fsanitize=address,undefined
    )
endif()

add_executable(koakuma
  src/api/rest_server.cpp
  src/catalog.cpp
  src/cross.cpp
  src/engine/buffer_manager.cpp
  src/engine/engine.cpp
  src/engine/file_manager.cpp
  src/file/common.cpp
  src/file/seq_file.cpp
  src/index/btree.cpp
  src/index/hash.cpp
  src/index/rtree.cpp
  src/layout/record_page.cpp
  src/layout/slotted_page.cpp
  src/main.cpp
  src/pack.cpp
  src/parser/ast.cpp
  src/parser/lexer.cpp
  src/parser/parser.cpp
  src/query_executor.cpp
  src/util.cpp
)

target_include_directories(koakuma
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated"
  PRIVATE external
  PRIVATE include
)

target_compile_options(koakuma PRIVATE -Wall -Wextra)

target_link_libraries(koakuma PRIVATE pthread)


include(GNUInstallDirs)

install(TARGETS koakuma
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
