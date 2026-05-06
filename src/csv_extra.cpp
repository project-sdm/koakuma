#include "csv_extra.hpp"
#include <string>
#include "rapidcsv/rapidcsv.h"

namespace rapidcsv {

    // allows converting csv cells to bools
    template<>
    void Converter<bool>::ToVal(const std::string& pStr, bool& pVal) const {
        if (pStr == "true")
            pVal = true;
        else if (pStr == "false")
            pVal = false;
        else
            throw std::invalid_argument("invalid bool");
    }

}  // namespace rapidcsv
