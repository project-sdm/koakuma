#include "buffer_manager.hpp"
#include <cassert>
#include "file_manager.hpp"

PageId::PageId(FileId fid, pnum_t pnum)
    : fid{fid},
      pnum{pnum} {}

[[nodiscard]] bool PageId::operator==(const PageId& other) const = default;
