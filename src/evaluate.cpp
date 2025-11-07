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

constexpr Value PassedPawnFifthBonus       = Value(PawnValue / 2);
constexpr Value PassedPawnSixthBonus       = Value(3 * PawnValue / 4);
constexpr Value PassedPawnSeventhBonus     = Value(5 * PawnValue / 4);
constexpr Value PassedPawnPushReadyBonus   = Value(PawnValue / 8);
constexpr Value KingCentralizationFactor   = Value(PawnValue / 8);
constexpr Value KingForwardActivityFactor  = Value(PawnValue / 12);
constexpr Value KingBlockadeFactor         = Value(PawnValue / 10);
constexpr Value PassiveRookPenalty         = Value(PawnValue / 2);
constexpr Value PassiveQueenPenalty        = Value(2 * PawnValue / 5);
constexpr Value ActiveRookBonus            = Value(PawnValue / 4);
constexpr Value ActiveQueenBonus           = Value(PawnValue / 5);
constexpr Value RemoteKingPenaltyFactor    = Value(PawnValue / 16);

bool is_basic_endgame(const Position& pos) {
    return pos.non_pawn_material() <= Value(2 * RookValue + 2 * BishopValue);
}

bool is_passed_pawn(const Position& pos, Color us, Square sq) {

    assert(pos.piece_on(sq) == make_piece(us, PAWN));

    const Color them    = ~us;
    const int   fileIdx = int(file_of(sq));
    const int   rankIdx = int(rank_of(sq));

    for (int df = -1; df <= 1; ++df)
    {
        const int file = fileIdx + df;
        if (file < int(FILE_A) || file > int(FILE_H))
            continue;

        if (us == WHITE)
        {
            for (int r = rankIdx + 1; r <= int(RANK_8); ++r)
            {
                Square ahead = make_square(File(file), Rank(r));
                if (pos.piece_on(ahead) == make_piece(them, PAWN))
                    return false;
            }
        }
        else
        {
            for (int r = rankIdx - 1; r >= int(RANK_1); --r)
            {
                Square ahead = make_square(File(file), Rank(r));
                if (pos.piece_on(ahead) == make_piece(them, PAWN))
                    return false;
            }
        }
    }

    return true;
}

Value passed_pawn_activity(const Position& pos, Color us) {

    Bitboard pawns = pos.pieces(us, PAWN);
    Value    bonus = VALUE_ZERO;

    while (pawns)
    {
        Square sq = pop_lsb(pawns);

        if (!is_passed_pawn(pos, us, sq))
            continue;

        Rank relRank = relative_rank(us, sq);

        if (relRank >= RANK_5)
            bonus += PassedPawnFifthBonus;

        if (relRank == RANK_6)
            bonus += PassedPawnSixthBonus;
        else if (relRank >= RANK_7)
            bonus += PassedPawnSeventhBonus;

        Square ahead = sq + pawn_push(us);
        if (is_ok(ahead) && pos.empty(ahead))
            bonus += PassedPawnPushReadyBonus;

        Square kingSq = pos.square<KING>(us);
        int    dist   = distance<Square>(kingSq, sq);
        if (dist < 4)
            bonus += Value((4 - dist) * (PawnValue / 16));
    }

    return bonus;
}

Value king_activity_bonus(const Position& pos, Color us) {

    Square kingSq = pos.square<KING>(us);
    Value  bonus  = VALUE_ZERO;

    const Square centers[] = {SQ_D4, SQ_E4, SQ_D5, SQ_E5};
    int          minDist   = 8;

    for (Square center : centers)
        minDist = std::min(minDist, distance<Square>(kingSq, center));

    if (minDist < 4)
        bonus += Value((4 - minDist) * KingCentralizationFactor);

    int relRank = int(relative_rank(us, kingSq));
    if (relRank > int(RANK_2))
        bonus += Value((relRank - int(RANK_2)) * KingForwardActivityFactor);

    Bitboard enemyPawns = pos.pieces(~us, PAWN);

    while (enemyPawns)
    {
        Square sq = pop_lsb(enemyPawns);

        if (!is_passed_pawn(pos, ~us, sq))
            continue;

        Square blockSq = sq + pawn_push(~us);
        if (!is_ok(blockSq))
            blockSq = sq;

        int dist = distance<Square>(kingSq, blockSq);
        if (dist <= 4)
            bonus += Value((4 - dist) * KingBlockadeFactor);
    }

    return bonus;
}

Value heavy_piece_activity_swing(const Position& pos, Color us) {

    if (pos.count<KNIGHT>(us) || pos.count<BISHOP>(us))
        return VALUE_ZERO;

    Bitboard pawns  = pos.pieces(us, PAWN);
    Bitboard rooks  = pos.pieces(us, ROOK);
    Bitboard queens = pos.pieces(us, QUEEN);
    Value    swing   = VALUE_ZERO;

    auto assess_heavy_piece = [&](Square sq, Value passivePenalty, Value activeBonus) {
        Bitboard sameFile = pawns & file_bb(sq);
        Bitboard tmp      = sameFile;
        bool     hasBonus = false;

        while (tmp)
        {
            Square pawnSq = pop_lsb(tmp);

            if ((us == WHITE && rank_of(sq) < rank_of(pawnSq))
                || (us == BLACK && rank_of(sq) > rank_of(pawnSq)))
            {
                swing -= passivePenalty;
                return;
            }

            if ((us == WHITE && rank_of(sq) > rank_of(pawnSq))
                || (us == BLACK && rank_of(sq) < rank_of(pawnSq)))
                hasBonus = true;
        }

        if (hasBonus)
            swing += activeBonus;
    };

    while (rooks)
        assess_heavy_piece(pop_lsb(rooks), PassiveRookPenalty, ActiveRookBonus);

    while (queens)
        assess_heavy_piece(pop_lsb(queens), PassiveQueenPenalty, ActiveQueenBonus);

    if (pawns)
    {
        Square leadPawn = us == WHITE ? Square(msb(pawns)) : Square(lsb(pawns));
        int    dist     = distance<Square>(pos.square<KING>(us), leadPawn);

        if (dist > 3)
            swing -= Value((dist - 3) * RemoteKingPenaltyFactor);
    }

    return swing;
}

Value endgame_activity_adjustment(const Position& pos) {

    if (!is_basic_endgame(pos))
        return VALUE_ZERO;

    Value adjustment[COLOR_NB] = {VALUE_ZERO, VALUE_ZERO};

    for (Color c : {WHITE, BLACK})
    {
        adjustment[c] += passed_pawn_activity(pos, c);
        adjustment[c] += king_activity_bonus(pos, c);
        adjustment[c] += heavy_piece_activity_swing(pos, c);
    }

    return adjustment[WHITE] - adjustment[BLACK];
}

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

    Value endgameAdjust = endgame_activity_adjustment(pos);
    if (pos.side_to_move() == BLACK)
        endgameAdjust = -endgameAdjust;
    nnue += endgameAdjust;

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
