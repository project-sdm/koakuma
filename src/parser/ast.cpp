#include "parser/ast.hpp"
#include <utility>
#include "parser/token.hpp"

namespace parser {
    Column::Column(std::string name, DataType type)
        : name{std::move(name)},
          type{type} {}

    RangeFilter::RangeFilter(f64 min_val, f64 max_val)
        : min_val{min_val},
          max_val{max_val} {}

    Point2D::Point2D(f64 x, f64 y)
        : x{x},
          y{y} {}

    RadFilter::RadFilter(Point2D origin, f64 radius)
        : origin{origin},
          radius{radius} {}

    KFilter::KFilter(Point2D origin, f64 k)
        : origin{origin},
          k{k} {}

    CreateStatement::CreateStatement(std::string table_name)
        : table_name{std::move(table_name)} {}

}  // namespace parser
