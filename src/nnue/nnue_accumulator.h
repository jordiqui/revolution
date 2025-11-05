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

// Class for difference calculation of NNUE evaluation function

#ifndef NNUE_ACCUMULATOR_H_INCLUDED
#define NNUE_ACCUMULATOR_H_INCLUDED

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../types.h"
#include "nnue_architecture.h"
#include "nnue_common.h"
#include "simd.h"

namespace Stockfish {
class Position;
}

namespace Stockfish::Eval::NNUE {

struct AccumulatorState;
struct Networks;

enum IncUpdateDirection {
    FORWARD,
    BACKWARDS
};

template<IndexType Size>
struct alignas(CacheLineSize) Accumulator;

template<IndexType TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> AccumulatorState::*AccPtr = nullptr>
class FeatureTransformer;

// Class that holds the result of affine transformation of input features
template<IndexType Size>
struct alignas(CacheLineSize) Accumulator {
    std::int16_t               accumulation[COLOR_NB][Size];
    std::int32_t               psqtAccumulation[COLOR_NB][PSQTBuckets];
    std::array<bool, COLOR_NB> computed = {};
};


// AccumulatorCaches struct provides per-thread accumulator caches, where each
// cache contains multiple entries for each of the possible king squares.
// When the accumulator needs to be refreshed, the cached entry is used to more
// efficiently update the accumulator, instead of rebuilding it from scratch.
// This idea, was first described by Luecx (author of Koivisto) and
// is commonly referred to as "Finny Tables".
struct AccumulatorCaches {

    template<typename Networks>
    AccumulatorCaches(const Networks& networks) {
        clear(networks);
    }

    template<IndexType Size>
    struct alignas(CacheLineSize) Cache {

        struct alignas(CacheLineSize) Entry {
            BiasType       accumulation[Size];
            PSQTWeightType psqtAccumulation[PSQTBuckets];
            Piece          pieces[SQUARE_NB];
            Bitboard       pieceBB;
            std::array<Bitboard, COLOR_NB>       byColorBB{};
            std::array<Bitboard, PIECE_TYPE_NB> byTypeBB{};

            // To initialize a refresh entry, we set all its bitboards empty,
            // so we put the biases in the accumulation, without any weights on top
            void clear(const BiasType* biases) {

                std::memcpy(accumulation, biases, sizeof(accumulation));
                std::memset((uint8_t*) this + offsetof(Entry, psqtAccumulation), 0,
                            sizeof(Entry) - offsetof(Entry, psqtAccumulation));
                byColorBB.fill(0);
                byTypeBB.fill(0);
            }
        };

        template<typename Network>
        void clear(const Network& network) {
            for (auto& entries1D : entries)
                for (auto& entry : entries1D)
                    entry.clear(network.featureTransformer.biases);
        }

        std::array<Entry, COLOR_NB>& operator[](Square sq) { return entries[sq]; }

        std::array<std::array<Entry, COLOR_NB>, SQUARE_NB> entries;
    };

    template<typename Networks>
    void clear(const Networks& networks) {
        big.clear(networks.big);
        small.clear(networks.small);
    }

    Cache<TransformedFeatureDimensionsBig>   big;
    Cache<TransformedFeatureDimensionsSmall> small;
};

template<IndexType Dimensions, IndexType>
struct SIMDTiling {
    static constexpr IndexType TileHeight = std::max<IndexType>(
      1, static_cast<IndexType>(::Stockfish::Eval::NNUE::SIMD::VecBytes / sizeof(BiasType)));
    static constexpr IndexType NumRegs        = 1;
    static constexpr IndexType PsqtTileHeight = std::max<IndexType>(
      1,
      static_cast<IndexType>(::Stockfish::Eval::NNUE::SIMD::VecBytes / sizeof(PSQTWeightType)));
    static constexpr std::size_t NumPsqtRegs = 1;
};


struct AccumulatorState {
    Accumulator<TransformedFeatureDimensionsBig>   accumulatorBig;
    Accumulator<TransformedFeatureDimensionsSmall> accumulatorSmall;
    DirtyPiece                                     dirtyPiece;

    template<IndexType Size>
    auto& acc() noexcept {
        static_assert(Size == TransformedFeatureDimensionsBig
                        || Size == TransformedFeatureDimensionsSmall,
                      "Invalid size for accumulator");

        if constexpr (Size == TransformedFeatureDimensionsBig)
            return accumulatorBig;
        else if constexpr (Size == TransformedFeatureDimensionsSmall)
            return accumulatorSmall;
    }

    template<IndexType Size>
    const auto& acc() const noexcept {
        static_assert(Size == TransformedFeatureDimensionsBig
                        || Size == TransformedFeatureDimensionsSmall,
                      "Invalid size for accumulator");

        if constexpr (Size == TransformedFeatureDimensionsBig)
            return accumulatorBig;
        else if constexpr (Size == TransformedFeatureDimensionsSmall)
            return accumulatorSmall;
    }

    void reset(const DirtyPiece& dp) noexcept;
};


class AccumulatorStack {
   public:
    [[nodiscard]] const AccumulatorState& latest() const noexcept;

    void reset(const Position& rootPos, const Networks& networks, AccumulatorCaches& caches) noexcept;
    void push(const DirtyPiece& dirtyPiece) noexcept;
    void pop() noexcept;

    template<IndexType Dimensions, Accumulator<Dimensions> AccumulatorState::*AccPtr>
    void evaluate(const Position&                                         pos,
                  const FeatureTransformer<Dimensions, AccPtr>&           featureTransformer,
                  AccumulatorCaches::Cache<Dimensions>&                   cache) noexcept;

   private:
    [[nodiscard]] AccumulatorState& mut_latest() noexcept;

    template<Color Perspective, IndexType Dimensions, Accumulator<Dimensions> AccumulatorState::*AccPtr>
    void evaluate_side(const Position&                                         pos,
                       const FeatureTransformer<Dimensions, AccPtr>&           featureTransformer,
                       AccumulatorCaches::Cache<Dimensions>&                   cache) noexcept;

    template<Color Perspective, IndexType Dimensions, Accumulator<Dimensions> AccumulatorState::*AccPtr>
    [[nodiscard]] std::size_t find_last_usable_accumulator() const noexcept;

    template<Color Perspective, IndexType Dimensions, Accumulator<Dimensions> AccumulatorState::*AccPtr>
    void forward_update_incremental(const Position&                           pos,
                                    const FeatureTransformer<Dimensions, AccPtr>& featureTransformer,
                                    const std::size_t                         begin) noexcept;

    template<Color Perspective, IndexType Dimensions, Accumulator<Dimensions> AccumulatorState::*AccPtr>
    void backward_update_incremental(const Position&                           pos,
                                     const FeatureTransformer<Dimensions, AccPtr>& featureTransformer,
                                     const std::size_t                         end) noexcept;

    std::array<AccumulatorState, MAX_PLY + 1> m_accumulators{};
    std::size_t                               m_current_idx = 1;
};

}  // namespace Stockfish::Eval::NNUE

#endif  // NNUE_ACCUMULATOR_H_INCLUDED
