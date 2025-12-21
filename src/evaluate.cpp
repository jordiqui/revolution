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

namespace {

int early_pawn_structure_penalty(const Position& pos, Color c) {
    // Encourage healthier pawn structures early in the game, with extra weight
    // on Black to discourage taking on structural weaknesses when defending.
    if (pos.game_ply() > 32)
        return 0;

    Bitboard pawns   = pos.pieces(c, PAWN);
    int      penalty = 0;

    for (File f = FILE_A; f <= FILE_H; ++f)
    {
        Bitboard pawnsOnFile = pawns & file_bb(f);
        int      countOnFile = popcount(pawnsOnFile);

        if (countOnFile >= 2)
            penalty += 8 * (countOnFile - 1);

        if (countOnFile)
        {
            Bitboard adjacentFiles = (f > FILE_A ? pawns & file_bb(File(f - 1)) : Bitboard(0))
                                   | (f < FILE_H ? pawns & file_bb(File(f + 1)) : Bitboard(0));
            if (!adjacentFiles)
                penalty += 10;
        }
    }

    // Fade the adjustment out of the opening and stress it more for Black.
    penalty = penalty * std::max(0, 28 - pos.game_ply()) / 28;
    return c == BLACK ? penalty * 5 / 4 : penalty;
}

int cautious_black_risk_adjustment(const Position& pos, Value eval) {
    if (pos.side_to_move() != BLACK)
        return 0;

    int materialBalance = Eval::simple_eval(pos);
    int adjustment      = 0;

    if (materialBalance < -PawnValue)
        adjustment += std::min(64, (-materialBalance / PawnValue) * 6 + 10);

    if (std::abs(eval) < 40)
    {
        int blackPieces = pos.count<KNIGHT>(BLACK) + pos.count<BISHOP>(BLACK)
                        + pos.count<ROOK>(BLACK) + pos.count<QUEEN>(BLACK);
        adjustment += blackPieces;
    }

    return adjustment;
}

int closed_position_safety_scale(const Position& pos) {
    const int rule50 = pos.rule50_count();

    if (rule50 < 12)
        return 0;

    const Bitboard whitePawns = pos.pieces(WHITE, PAWN);
    const Bitboard blackPawns = pos.pieces(BLACK, PAWN);
    const int      totalPawns = popcount(whitePawns | blackPawns);

    if (!totalPawns)
        return 0;

    const int blockedWhite = popcount(shift<NORTH>(whitePawns) & blackPawns);
    const int blockedBlack = popcount(shift<SOUTH>(blackPawns) & whitePawns);
    const int blocked      = blockedWhite + blockedBlack;

    if (blocked * 5 < totalPawns * 3)
        return 0;

    const int lockScore    = blocked * 64 / totalPawns;
    const int shuffleScore = std::min(64, (rule50 - 10) * 2);

    return std::min(96, lockScore + shuffleScore);
}

}  // namespace

// Returns a static, purely materialistic evaluation of the position from
// the point of view of the side to move. It can be divided by PawnValue to get
// an approximation of the material advantage on the board in terms of pawns.
int Eval::simple_eval(const Position& pos) {
    Color c = pos.side_to_move();
    return PawnValue * (pos.count<PAWN>(c) - pos.count<PAWN>(~c))
         + (pos.non_pawn_material(c) - pos.non_pawn_material(~c));
}

bool Eval::use_smallnet(const Position& pos) { return std::abs(simple_eval(pos)) > 962; }

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
    optimism += optimism * nnueComplexity / 468;
    nnue -= nnue * nnueComplexity / 18000;

    int material = 535 * pos.count<PAWN>() + pos.non_pawn_material();
    int v        = (nnue * (77777 + material) + optimism * (7777 + material)) / 77777;

    int pawnPenalty[COLOR_NB] = {early_pawn_structure_penalty(pos, WHITE),
                                 early_pawn_structure_penalty(pos, BLACK)};
    v -= pawnPenalty[pos.side_to_move()] - pawnPenalty[~pos.side_to_move()];

    v -= cautious_black_risk_adjustment(pos, v);

    const int safetyScale = closed_position_safety_scale(pos);
    v -= v * safetyScale / 256;

    // Damp down the evaluation linearly when shuffling
    v -= v * pos.rule50_count() / 212;

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

    auto accumulators = std::make_unique<Eval::NNUE::AccumulatorStack>();
    auto caches       = std::make_unique<Eval::NNUE::AccumulatorCaches>(networks);

    std::stringstream ss;
    ss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2);
    ss << '\n' << NNUE::trace(pos, networks, *caches) << '\n';

    ss << std::showpoint << std::showpos << std::fixed << std::setprecision(2) << std::setw(15);

    auto [psqt, positional] = networks.big.evaluate(pos, *accumulators, &caches->big);
    Value v                 = psqt + positional;
    v                       = pos.side_to_move() == WHITE ? v : -v;
    ss << "NNUE evaluation        " << 0.01 * UCIEngine::to_cp(v, pos) << " (white side)\n";

    v = evaluate(networks, pos, *accumulators, *caches, VALUE_ZERO);
    v = pos.side_to_move() == WHITE ? v : -v;
    ss << "Final evaluation       " << 0.01 * UCIEngine::to_cp(v, pos) << " (white side)";
    ss << " [with scaled NNUE, ...]";
    ss << "\n";

    return ss.str();
}

}  // namespace Stockfish
