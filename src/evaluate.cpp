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

#include "evaluate.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <tuple>

#include "nnue/network.h"
#include "nnue/nnue_misc.h"
#include "position.h"
#include "types.h"
#include "uci.h"
#include "nnue/nnue_accumulator.h"

namespace Stockfish {

// Returns a static, purely materialistic evaluation of the position from
// the point of view of the given color. It can be divided by PawnValue to get
// an approximation of the material advantage on the board in terms of pawns.
int Eval::simple_eval(const Position& pos, Color c) {
    return PawnValue * (pos.count<PAWN>(c) - pos.count<PAWN>(~c))
         + (pos.non_pawn_material(c) - pos.non_pawn_material(~c));
}

bool Eval::use_smallnet(const Position& pos) {
    int simpleEval = simple_eval(pos, pos.side_to_move());

    int nonPawnCount = pos.count<KNIGHT>() + pos.count<BISHOP>() + pos.count<ROOK>()
                     + pos.count<QUEEN>();
    int queenCount   = pos.count<QUEEN>();
    int pawnCount    = pos.count<PAWN>();
    int phase        = std::clamp(14 - nonPawnCount, 0, 14);
    int fiftyCount   = pos.rule50_count();

    // The small network is accurate in simplified technical endings, but in the
    // regression games DEV often entered complex winning or defending scenarios
    // (multiple major pieces on board) where relying on the faster network lead
    // to serious misjudgements and over-optimistic play.  Restrict its usage to
    // positions with few remaining non-pawn pieces and no queens so we keep the
    // full-size network for the sharp conversions highlighted in the matches.
    if (queenCount > 0 || nonPawnCount > 6)
        return false;

    int dryFactor      = std::max(0, 8 - pawnCount / 2) + std::max(0, phase - 4);
    int fiftyAdjustment = fiftyCount * (4 + dryFactor) / 16;
    int threshold        = 880 + phase * 18 + fiftyAdjustment;
    threshold            = std::clamp(threshold, 720, 1400);

    return std::abs(simpleEval) > threshold;
}

// Evaluate is the evaluator for the outer world. It returns a static evaluation
// of the position from the point of view of the side to move.
Value Eval::evaluate(const Eval::NNUE::Networks&    networks,
                     const Position&                pos,
                     Eval::NNUE::AccumulatorStack&  accumulators,
                     Eval::NNUE::AccumulatorCaches& caches,
                     int                            optimism) {

    assert(!pos.checkers());

    bool smallNet           = use_smallnet(pos);
    auto [psqt, positional] = smallNet ? networks.small.evaluate(pos, accumulators, &caches.small)
                                       : networks.big.evaluate(pos, accumulators, &caches.big);

    Value nnue = (125 * psqt + 131 * positional) / 128;

    // Re-evaluate the position when higher eval accuracy is worth the time spent
    if (smallNet && (std::abs(nnue) < 236))
    {
        std::tie(psqt, positional) = networks.big.evaluate(pos, accumulators, &caches.big);
        nnue                       = (125 * psqt + 131 * positional) / 128;
        smallNet                   = false;
    }

    // Blend optimism and eval with nnue complexity
    int nnueComplexity = std::abs(psqt - positional);
    int compressedComplexity =
      std::min(4096, int(std::sqrt(static_cast<double>(nnueComplexity))) * 64);
    optimism += optimism * compressedComplexity / 2048;
    nnue -= nnue * compressedComplexity / 28000;

    int material = 535 * pos.count<PAWN>() + pos.non_pawn_material();
    int v        = (nnue * (77777 + material) + optimism * (7777 + material)) / 77777;

    // Damp down the evaluation with sensitivity to piece count and remaining advantage
    int rule50 = pos.rule50_count();
    if (rule50)
    {
        int pieces = pos.count<PAWN>() + pos.count<KNIGHT>() + pos.count<BISHOP>()
                   + pos.count<ROOK>() + pos.count<QUEEN>();
        int dryness = std::max(0, 12 - pieces);
        int advShield = std::clamp(std::abs(v) - 180, 0, 640);
        int weight    = std::clamp(48 + dryness * 8 - advShield / 4, 12, 96);
        int64_t damping = int64_t(v) * rule50 * weight / (212 * 64);
        v -= Value(damping);
    }

    // Guarantee evaluation does not hit the tablebase range
    v = std::clamp(v, VALUE_TB_LOSS_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);

    return v;
}

// Like evaluate(), but instead of returning a value, it returns
// a string (suitable for outputting to stdout) that contains the detailed
// descriptions and values of each evaluation term. Useful for debugging.
// Trace scores are from white's point of view
std::string Eval::trace(Position& pos, const Eval::NNUE::Networks& networks) {

    if (pos.checkers())
        return "Final evaluation: none (in check)";

    Eval::NNUE::AccumulatorStack accumulators;
    auto                         caches = std::make_unique<Eval::NNUE::AccumulatorCaches>(networks);

    accumulators.reset(pos, networks, *caches);

    std::stringstream ss;
    ss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2);
    ss << '\n' << NNUE::trace(pos, networks, *caches) << '\n';

    ss << std::showpoint << std::showpos << std::fixed << std::setprecision(2) << std::setw(15);

    auto [psqt, positional] = networks.big.evaluate(pos, accumulators, &caches->big);
    Value v                 = psqt + positional;
    v                       = pos.side_to_move() == WHITE ? v : -v;
    ss << "NNUE evaluation        " << 0.01 * UCIEngine::to_cp(v, pos) << " (white side)\n";

    v = evaluate(networks, pos, accumulators, *caches, VALUE_ZERO);
    v = pos.side_to_move() == WHITE ? v : -v;
    ss << "Final evaluation       " << 0.01 * UCIEngine::to_cp(v, pos) << " (white side)";
    ss << " [with scaled NNUE, ...]";
    ss << "\n";

    return ss.str();
}

}  // namespace Stockfish
