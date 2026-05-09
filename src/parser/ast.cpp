#include "parser/ast.hpp"
#include <utility>
#include "parser/token.hpp"

namespace parser {

    Column::Column(std::string name, DataType type)
        : name{std::move(name)},
          type{type} {}

    RangeFilter::RangeFilter(ExprLit low, ExprLit high)
        : low{std::move(low)},
          high{std::move(high)} {}

    Point2D::Point2D() = default;

    Point2D::Point2D(f64 x, f64 y)
        : x{x},
          y{y} {}

    bool Point2D::operator==(const Point2D& other) const = default;

    bool Point2D::operator<(const Point2D& other) const {
        if (x != other.x)
            return x < other.x;

        return y < other.y;
    }

    bool Point2D::operator<=(const Point2D& other) const {
        return *this == other || *this < other;
    }

    bool Point2D::operator>(const Point2D& other) const {
        if (x != other.x)
            return x > other.x;

        return y > other.y;
    }
    bool Point2D::operator>=(const Point2D& other) const {
        return *this == other || *this > other;
    }

    RadFilter::RadFilter(Point2D origin, f64 radius)
        : origin{origin},
          radius{radius} {}

    KFilter::KFilter(Point2D origin, u64 k)
        : origin{origin},
          k{k} {}

    CreateStatement::CreateStatement(std::string table_name)
        : table_name{std::move(table_name)} {}

}  // namespace parser
