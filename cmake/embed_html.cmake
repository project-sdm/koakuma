# Reads INPUT (an HTML file) and writes OUTPUT (a C++ header) that embeds
# the content as an inline std::string_view.

cmake_minimum_required(VERSION 3.11)

if(NOT DEFINED INPUT OR NOT DEFINED VENDOR_DIR OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "embed_html.cmake requires -D INPUT=<path> -D VENDOR_DIR=<path> -D OUTPUT=<path>")
endif()

# ── Read vendor CSS ───────────────────────────────────────────────────────────
file(READ "${VENDOR_DIR}/codemirror.min.css" css1)
file(READ "${VENDOR_DIR}/show-hint.min.css"  css2)
set(vendor_css "<style>\n${css1}\n${css2}\n</style>")

# ── Read vendor JS ────────────────────────────────────────────────────────────
file(READ "${VENDOR_DIR}/codemirror.min.js" js1)
file(READ "${VENDOR_DIR}/sql.js"            js2)
file(READ "${VENDOR_DIR}/matchbrackets.js"  js3)
file(READ "${VENDOR_DIR}/closebrackets.js"  js4)
file(READ "${VENDOR_DIR}/show-hint.js"      js5)
file(READ "${VENDOR_DIR}/sql-hint.js"       js6)
file(READ "${VENDOR_DIR}/active-line.js"    js7)
set(vendor_js "<script>\n${js1}\n${js2}\n${js3}\n${js4}\n${js5}\n${js6}\n${js7}\n</script>")

# ── Replace placeholders ──────────────────────────────────────────────────────
file(READ "${INPUT}" html_content)
string(REPLACE "<!-- @vendor-css -->" "${vendor_css}" html_content "${html_content}")
string(REPLACE "<!-- @vendor-js -->"  "${vendor_js}"  html_content "${html_content}")

# ── Write header ──────────────────────────────────────────────────────────────
file(WRITE "${OUTPUT}"
"#pragma once

#include <string_view>

// Auto-generated from ${INPUT} — do not edit manually.

namespace api {

inline std::string_view FRONTEND_HTML = R\"KKFRONTEND(${html_content})KKFRONTEND\";

}  // namespace api
")
