#include "file/common.hpp"
#include <utility>

UnknownFile::Header::Header() = default;

UnknownFile::Header::Header(FileType file_type, std::vector<Column> columns, u32 pkey_col)
    : file_type{file_type},
      columns{std::move(columns)},
      pkey_col{pkey_col} {}

Rid::Rid() = default;

Rid::Rid(pnum_t pnum, u32 slot_idx)
    : pnum{pnum},
      slot_idx{slot_idx} {}

bool Rid::operator==(const Rid& other) const = default;

Column::Column() = default;

Column::Column(std::string name, ColumnType type, std::optional<IndexType> index)
    : name{std::move(name)},
      type{type},
      index{index} {}
