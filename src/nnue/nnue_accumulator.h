/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Class for difference calculation of NNUE evaluation function

#ifndef NNUE_ACCUMULATOR_H_INCLUDED
#define NNUE_ACCUMULATOR_H_INCLUDED

#include <array>
#include <cstddef>
#include <cstring>
#include <utility>

#include "../types.h"
#include "../misc.h"
#include "nnue_architecture.h"
#include "nnue_common.h"

namespace Stockfish {
class Position;
}

namespace Stockfish::Eval::NNUE {

template<IndexType Size>
struct alignas(CacheLineSize) Accumulator;

template<IndexType TransformedFeatureDimensions>
class FeatureTransformer;

using ActiveFeatureTransformer = FeatureTransformer<ActiveTransformedFeatureDimensions>;

// Class that holds the result of affine transformation of input features,
// combined HalfKA + Threats
template<IndexType Size>
struct alignas(CacheLineSize) Accumulator {
    std::array<std::array<i16, Size>, COLOR_NB>        accumulation;
    std::array<std::array<i32, PSQTBuckets>, COLOR_NB> psqtAccumulation;
    std::array<bool, COLOR_NB>                         computed = {};
};


// AccumulatorCaches struct provides per-thread accumulator caches, where each
// cache contains multiple entries for each of the possible king squares.
// When the accumulator needs to be refreshed, the cached entry is used to more
// efficiently update the accumulator, instead of rebuilding it from scratch.
// This idea, was first described by Luecx (author of Koivisto) and
// is commonly referred to as "Finny Tables".
struct AccumulatorCaches {
    AccumulatorCaches() = default;

    template<IndexType Size>
    struct alignas(CacheLineSize) Cache {
        struct alignas(CacheLineSize) Entry {
            std::array<BiasType, Size>              accumulation;
            std::array<PSQTWeightType, PSQTBuckets> psqtAccumulation;
            std::array<Piece, SQUARE_NB>            pieces;
            Bitboard                                pieceBB;

            void clear(const std::array<BiasType, Size>& biases) {
                accumulation = biases;
                std::memset(reinterpret_cast<std::byte*>(this) + offsetof(Entry, psqtAccumulation),
                            0, sizeof(Entry) - offsetof(Entry, psqtAccumulation));
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

    template<typename Network>
    void init_big(const Network& network) { big.clear(network); }

    template<typename Network>
    void clear_big(const Network& network) { big.clear(network); }

    Cache<TransformedFeatureDimensionsBig> big;
};

using ActiveAccumulatorCache = AccumulatorCaches::Cache<ActiveTransformedFeatureDimensions>;
using ActiveAccumulatorCaches = AccumulatorCaches;


struct AccumulatorState: public Accumulator<ActiveTransformedFeatureDimensions> {
    DirtyPiece   dirtyPiece;
    DirtyThreats dirtyThreats;
};

using ActiveAccumulator = Accumulator<ActiveTransformedFeatureDimensions>;
using ActivePSQAccumulatorState = AccumulatorState;
using ActiveThreatAccumulatorState = AccumulatorState;

class AccumulatorStack {
   public:
    static constexpr usize MaxSize = MAX_PLY + 1;

    [[nodiscard]] const AccumulatorState& latest() const noexcept;

    void                                  reset() noexcept;
    std::pair<DirtyPiece&, DirtyThreats&> push() noexcept;
    void                                  pop() noexcept;

    void evaluate(const Position&           pos,
                  const ActiveFeatureTransformer& featureTransformer,
                  // Silence spurious warning on GCC 10
                  [[maybe_unused]] ActiveAccumulatorCache& cache) noexcept;

   private:
    [[nodiscard]] AccumulatorState& mut_latest() noexcept;

    void evaluate_side(Color                     perspective,
                       const Position&           pos,
                       const ActiveFeatureTransformer& featureTransformer,
                       // Silence spurious warning on GCC 10
                       [[maybe_unused]] ActiveAccumulatorCache& cache) noexcept;

    [[nodiscard]] usize find_last_usable_accumulator(Color perspective) const noexcept;

    void forward_update_incremental(Color                     perspective,
                                    const Position&           pos,
                                    const ActiveFeatureTransformer& featureTransformer,
                                    const usize               begin) noexcept;

    void backward_update_incremental(Color                     perspective,
                                     const Position&           pos,
                                     const ActiveFeatureTransformer& featureTransformer,
                                     const usize               end) noexcept;

    std::array<AccumulatorState, MaxSize> accumulators;
    usize                                 size = 1;
};

}  // namespace Stockfish::Eval::NNUE

#endif  // NNUE_ACCUMULATOR_H_INCLUDED
