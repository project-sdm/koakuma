cmake_minimum_required(VERSION 3.11)
project(koakuma LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED 23)
set(CMAKE_CXX_STANDARD_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# ── Embed frontend HTML ───────────────────────────────────────────────────────
set(FRONTEND_HTML_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/api/frontend.html")
set(FRONTEND_VENDOR   "${CMAKE_CURRENT_SOURCE_DIR}/src/api/vendor")
set(FRONTEND_HPP_GEN  "${CMAKE_CURRENT_BINARY_DIR}/generated/api/frontend.hpp")

file(GLOB FRONTEND_VENDOR_FILES "${FRONTEND_VENDOR}/*")

add_custom_command(
    OUTPUT  "${FRONTEND_HPP_GEN}"
    COMMAND "${CMAKE_COMMAND}"
            -D "INPUT=${FRONTEND_HTML_SRC}"
            -D "VENDOR_DIR=${FRONTEND_VENDOR}"
            -D "OUTPUT=${FRONTEND_HPP_GEN}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_html.cmake"
    DEPENDS "${FRONTEND_HTML_SRC}"
            ${FRONTEND_VENDOR_FILES}
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_html.cmake"
    COMMENT "Embedding frontend.html -> generated/api/frontend.hpp"
    VERBATIM
)

add_custom_target(generate_frontend DEPENDS "${FRONTEND_HPP_GEN}")

add_executable(koakuma
  src/api/rest_server.cpp
  src/catalog.cpp
  src/cross.cpp
  src/engine/buffer_manager.cpp
  src/engine/engine.cpp
  src/engine/file_manager.cpp
  src/index/btree.cpp
  src/index/hash.cpp
  src/layout/slotted_page.cpp
  src/main.cpp
  src/pack.cpp
  src/parser/ast.cpp
  src/parser/lexer.cpp
  src/parser/parser.cpp
  src/parser/peekable_lexer.cpp
  src/query_executor.cpp
  src/seq_file.cpp
  src/util.cpp
)

add_dependencies(koakuma generate_frontend)

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
