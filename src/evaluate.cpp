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
#include "bitboard.h"

namespace Stockfish {

namespace {

constexpr Value WingAttackBasePenalty      = 8;
constexpr Value WingAttackDepthPenalty     = 3;
constexpr Value WingFileAlignmentPenalty   = 2;
constexpr Value CounterplayAdvancedBonus   = 16;
constexpr Value CounterplayReadyBonus      = 12;
constexpr Value CounterplayPreparation     = 8;
constexpr Value PassiveHFilePenalty        = 120;
constexpr Value MaximumSwing               = 80;

Value counterplay_strength(const Position& pos, Color us, File file) {

    Bitboard pawns = pos.pieces(us, PAWN) & file_bb(file);
    Value    best  = VALUE_ZERO;

    while (pawns)
    {
        Square sq     = pop_lsb(pawns);
        Rank   rel    = relative_rank(us, sq);
        Value  score  = VALUE_ZERO;
        Square ahead  = sq + pawn_push(us);
        bool   canUse = is_ok(ahead) && pos.empty(ahead);

        if (rel >= RANK_5)
            score = CounterplayAdvancedBonus;
        else if (canUse)
        {
            Rank aheadRank = relative_rank(us, ahead);

            if (aheadRank >= RANK_5)
                score = CounterplayReadyBonus;
            else if (rel == RANK_2)
            {
                Square ahead2 = ahead + pawn_push(us);

                if (is_ok(ahead2) && pos.empty(ahead2))
                    score = CounterplayPreparation;
            }
            else if (aheadRank == RANK_4)
                score = CounterplayPreparation;
        }

        best = std::max(best, score);
    }

    return best;
}

bool kingside_fianchetto(const Position& pos, Color us) {

    Square kingSq = pos.square<KING>(us);

    if (file_of(kingSq) < FILE_F)
        return false;

    Square gHome      = relative_square(us, SQ_G2);
    Square gAdvance   = gHome + pawn_push(us);
    bool   gAdvanceOk = is_ok(gAdvance);
    bool   gPawn      = pos.piece_on(gHome) == make_piece(us, PAWN)
                     || (gAdvanceOk && pos.piece_on(gAdvance) == make_piece(us, PAWN));

    if (!gPawn)
        return false;

    Square bishopSq = relative_square(us, SQ_G7);
    Square hHome    = relative_square(us, SQ_H2);
    Square hAdvance = hHome + pawn_push(us);
    bool   bishop   = is_ok(bishopSq) && pos.piece_on(bishopSq) == make_piece(us, BISHOP);
    bool   hPawn    = pos.piece_on(hHome) == make_piece(us, PAWN)
                   || (is_ok(hAdvance) && pos.piece_on(hAdvance) == make_piece(us, PAWN));

    return bishop || hPawn;
}

Value dynamic_king_safety_for(const Position& pos, Color us) {

    if (!kingside_fianchetto(pos, us))
        return VALUE_ZERO;

    const Color them    = ~us;
    Square      kingSq  = pos.square<KING>(us);
    Bitboard    wing    = pos.pieces(them, PAWN) & (file_bb(FILE_G) | file_bb(FILE_H));
    Value       threat  = VALUE_ZERO;
    int         attackers = 0;

    while (wing)
    {
        Square sq = pop_lsb(wing);
        Rank   rr = relative_rank(them, sq);

        if (rr < RANK_4)
            continue;

        ++attackers;
        threat += WingAttackBasePenalty;
        threat += WingAttackDepthPenalty * (rr - RANK_4);

        if (std::abs(file_of(sq) - file_of(kingSq)) <= 1)
            threat += WingFileAlignmentPenalty;
    }

    if (!attackers)
        return VALUE_ZERO;

    Value counterplay = counterplay_strength(pos, us, FILE_C)
                      + counterplay_strength(pos, us, FILE_F)
                      + counterplay_strength(pos, us, FILE_E) / 2;

    Value swing = counterplay * attackers - threat;

    Square passiveH = relative_square(us, SQ_H3);
    bool   passive  = pos.piece_on(passiveH) == make_piece(us, PAWN);

    if (passive && counterplay < CounterplayReadyBonus)
        swing -= PassiveHFilePenalty;

    return std::clamp(swing, -MaximumSwing, MaximumSwing);
}

Value dynamic_king_safety_adjustment(const Position& pos) {

    Value white = dynamic_king_safety_for(pos, WHITE);
    Value black = dynamic_king_safety_for(pos, BLACK);

    return std::clamp(white - black, -MaximumSwing, MaximumSwing);
}

}  // namespace

// Returns a static, purely materialistic evaluation of the position from
// the point of view of the given color. It can be divided by PawnValue to get
// an approximation of the material advantage on the board in terms of pawns.
int Eval::simple_eval(const Position& pos, Color c) {
    return PawnValue * (pos.count<PAWN>(c) - pos.count<PAWN>(~c))
         + (pos.non_pawn_material(c) - pos.non_pawn_material(~c));
}

bool Eval::use_smallnet(const Position& pos) {
    int simpleEval = simple_eval(pos, pos.side_to_move());
    return std::abs(simpleEval) > 962;
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

    Value dynamicAdjust = dynamic_king_safety_adjustment(pos);
    if (pos.side_to_move() == BLACK)
        dynamicAdjust = -dynamicAdjust;
    nnue += dynamicAdjust;

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

    accumulators->reset(pos, networks, *caches);

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
