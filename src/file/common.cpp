#include "file/common.hpp"
#include <utility>

UnknownFile::Header::Header() = default;

UnknownFile::Header::Header(FileType file_type, std::vector<Column> columns, u32 pkey_col)
    : file_type{file_type},
      columns{std::move(columns)},
      pkey_col{pkey_col} {}
