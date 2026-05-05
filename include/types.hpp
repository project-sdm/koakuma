#ifndef TYPES_HPP
#define TYPES_HPP

#include <climits>
#include <cstdint>
#include <stdfloat>

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

#ifdef __STDCPP_FLOAT16_T__
using f16 = std::float16_t;
#endif

#ifdef __STDCPP_FLOAT32_T__
using f32 = std::float16_t;
#else
using f32 = float;
static_assert(sizeof(f32) == 32 / CHAR_BIT);
#endif

#ifdef __STDCPP_FLOAT64_T__
using f64 = std::float64_t;
#else
using f64 = double;
static_assert(sizeof(f64) == 64 / CHAR_BIT);
#endif

#ifdef __STDCPP_FLOAT128_T__
using f128 = std::float128_t;
#endif

#endif
