#include "util.hpp"
#include <string>
#include <string_view>

namespace util {

    std::string getenv_or(const char* name, std::string_view default_value) {
        const char* val = std::getenv(name);  // NOLINT

        if (val == nullptr)
            return std::string{default_value};

        return val;
    }

}  // namespace util
