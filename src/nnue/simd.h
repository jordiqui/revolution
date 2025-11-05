/*
  Revolution, a UCI chess playing engine derived from Stockfish 17.1
  Copyright (C) 2004-2025 The Stockfish developers (see AUTHORS file)

  Revolution is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Revolution is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef NNUE_SIMD_H_INCLUDED
#define NNUE_SIMD_H_INCLUDED

#include <algorithm>
#include <array>
#include <cstdint>

#include "nnue_common.h"

namespace Stockfish::Eval::NNUE::SIMD {

constexpr std::size_t VecBytes = 16;
constexpr std::size_t VecInt16Count = VecBytes / sizeof(BiasType);
constexpr std::size_t VecInt32Count = VecBytes / sizeof(PSQTWeightType);

struct alignas(VecBytes) vec_t {
    std::array<std::uint8_t, VecBytes> bytes{};

    std::int16_t* as_i16() { return reinterpret_cast<std::int16_t*>(bytes.data()); }
    const std::int16_t* as_i16() const { return reinterpret_cast<const std::int16_t*>(bytes.data()); }
};

struct alignas(VecBytes) psqt_vec_t {
    std::array<std::uint8_t, VecBytes> bytes{};

    std::int32_t* as_i32() { return reinterpret_cast<std::int32_t*>(bytes.data()); }
    const std::int32_t* as_i32() const { return reinterpret_cast<const std::int32_t*>(bytes.data()); }
};

inline vec_t vec_zero() { return vec_t{}; }

inline vec_t vec_set_16(int value) {
    vec_t out;
    auto* data = out.as_i16();
    for (std::size_t i = 0; i < VecInt16Count; ++i)
        data[i] = static_cast<std::int16_t>(value);
    return out;
}

inline vec_t vec_min_16(const vec_t& a, const vec_t& b) {
    vec_t out;
    auto* dst       = out.as_i16();
    const auto* lhs = a.as_i16();
    const auto* rhs = b.as_i16();
    for (std::size_t i = 0; i < VecInt16Count; ++i)
        dst[i] = std::min(lhs[i], rhs[i]);
    return out;
}

inline vec_t vec_max_16(const vec_t& a, const vec_t& b) {
    vec_t out;
    auto* dst       = out.as_i16();
    const auto* lhs = a.as_i16();
    const auto* rhs = b.as_i16();
    for (std::size_t i = 0; i < VecInt16Count; ++i)
        dst[i] = std::max(lhs[i], rhs[i]);
    return out;
}

inline vec_t vec_slli_16(const vec_t& v, int shift) {
    vec_t out;
    auto* dst = out.as_i16();
    const auto* src = v.as_i16();
    for (std::size_t i = 0; i < VecInt16Count; ++i)
        dst[i] = static_cast<std::int16_t>(static_cast<std::int32_t>(src[i]) << shift);
    return out;
}

inline vec_t vec_mulhi_16(const vec_t& a, const vec_t& b) {
    vec_t out;
    auto* dst       = out.as_i16();
    const auto* lhs = a.as_i16();
    const auto* rhs = b.as_i16();
    for (std::size_t i = 0; i < VecInt16Count; ++i)
    {
        std::int32_t prod = static_cast<std::int32_t>(lhs[i]) * static_cast<std::int32_t>(rhs[i]);
        dst[i]            = static_cast<std::int16_t>(prod >> 16);
    }
    return out;
}

inline vec_t vec_add_16(const vec_t& a, const vec_t& b) {
    vec_t out;
    auto* dst       = out.as_i16();
    const auto* lhs = a.as_i16();
    const auto* rhs = b.as_i16();
    for (std::size_t i = 0; i < VecInt16Count; ++i)
        dst[i] = static_cast<std::int16_t>(static_cast<std::int32_t>(lhs[i])
                                           + static_cast<std::int32_t>(rhs[i]));
    return out;
}

inline vec_t vec_sub_16(const vec_t& a, const vec_t& b) {
    vec_t out;
    auto* dst       = out.as_i16();
    const auto* lhs = a.as_i16();
    const auto* rhs = b.as_i16();
    for (std::size_t i = 0; i < VecInt16Count; ++i)
        dst[i] = static_cast<std::int16_t>(static_cast<std::int32_t>(lhs[i])
                                           - static_cast<std::int32_t>(rhs[i]));
    return out;
}

inline vec_t vec_packus_16(const vec_t& lo, const vec_t& hi) {
    vec_t out;
    auto* bytes = out.bytes.data();
    const auto* l = lo.as_i16();
    const auto* h = hi.as_i16();
    for (std::size_t i = 0; i < VecInt16Count; ++i)
        bytes[i] = static_cast<std::uint8_t>(std::clamp<int>(l[i], 0, 255));
    for (std::size_t i = 0; i < VecInt16Count; ++i)
        bytes[VecInt16Count + i] = static_cast<std::uint8_t>(std::clamp<int>(h[i], 0, 255));
    return out;
}

inline void vec_store(vec_t* dst, const vec_t& value) { *dst = value; }

inline psqt_vec_t vec_add_psqt_32(const psqt_vec_t& a, const psqt_vec_t& b) {
    psqt_vec_t out;
    auto* dst       = out.as_i32();
    const auto* lhs = a.as_i32();
    const auto* rhs = b.as_i32();
    for (std::size_t i = 0; i < VecInt32Count; ++i)
        dst[i] = lhs[i] + rhs[i];
    return out;
}

inline psqt_vec_t vec_sub_psqt_32(const psqt_vec_t& a, const psqt_vec_t& b) {
    psqt_vec_t out;
    auto* dst       = out.as_i32();
    const auto* lhs = a.as_i32();
    const auto* rhs = b.as_i32();
    for (std::size_t i = 0; i < VecInt32Count; ++i)
        dst[i] = lhs[i] - rhs[i];
    return out;
}

inline void vec_store_psqt(psqt_vec_t* dst, const psqt_vec_t& value) { *dst = value; }

}  // namespace Stockfish::Eval::NNUE::SIMD

#endif  // NNUE_SIMD_H_INCLUDED

