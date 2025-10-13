/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2025 The Stockfish developers (see AUTHORS file)

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

#pragma once

#include <string>
#include <string_view>

#include "types.h"

namespace Stockfish {

class Position;

namespace Eval {

// The default net name MUST follow the format nn-[SHA256 first 12 digits].nnue
// for the build process (profile-build and fishtest) to work. Do not change the
// names or the location where these constants are defined, as they are used in
// the Makefile/Fishtest.
inline constexpr std::string_view EvalFileDefaultNameBig   = "nn-1c0000000000.nnue";
inline constexpr std::string_view EvalFileDefaultNameSmall = "nn-37f18f62d772.nnue";

namespace NNUE {
struct Networks;
struct AccumulatorCaches;
class AccumulatorStack;
}

std::string trace(Position& pos, const Eval::NNUE::Networks& networks);

int   simple_eval(const Position& pos);
bool  use_smallnet(const Position& pos);
Value evaluate(const NNUE::Networks&          networks,
               const Position&                pos,
               Eval::NNUE::AccumulatorStack&  accumulators,
               Eval::NNUE::AccumulatorCaches& caches,
               int                            optimism);

// Toggle for optional style-based evaluation adjustments.
void set_adaptive_style(bool enabled);
// Toggles for experimental evaluation heuristics that are prototyped via self-play.
void set_dark_square_coverage(bool enabled);
void set_soft_knight_outposts(bool enabled);

namespace detail {
int  passed_pawn_pressure(const Position& pos, Color defender);
bool is_passed_pawn(const Position& pos, Color side, Square sq);
}  // namespace detail
}  // namespace Eval

}  // namespace Stockfish

