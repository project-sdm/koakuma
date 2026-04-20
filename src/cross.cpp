#include "cross.hpp"
#include <cstddef>

#if __has_include(<unistd.h>)

#include <unistd.h>

std::size_t cross::get_page_size() {
    return sysconf(_SC_PAGESIZE);
}

#else

#include <windows.h>

long cross::get_page_size() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return sysInfo.dwPageSize;
}

#endif
