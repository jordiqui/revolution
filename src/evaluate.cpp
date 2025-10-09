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
#include <mutex>
#include <sstream>
#include <string>
#include <tuple>

#include "nnue/network.h"
#include "nnue/nnue_misc.h"
#include "position.h"
#include "types.h"
#include "uci.h"
#include "nnue/nnue_accumulator.h"

namespace Stockfish {

// Per-thread evaluation cache (optional)
// Define USE_FAST_EVAL_CACHE to enable a fast per-thread direct-mapped cache.
// Compile with e.g. -DUSE_FAST_EVAL_CACHE to activate it.
namespace {
#ifdef USE_FAST_EVAL_CACHE
// Full-featured cache (direct-mapped, 8192 entries by default).
struct EvalCacheEntry {
    uint64_t key           = 0;  // position key
    int32_t  last_optimism = 0;  // optimism for which `v` was computed
    Value    v             = 0;  // final cached evaluation (for last_optimism)

    // cached NNUE outputs for small network
    Value   psqt_small       = 0;
    Value   positional_small = 0;
    uint8_t have_small       = 0;

    // cached NNUE outputs for big network
    Value   psqt_big       = 0;
    Value   positional_big = 0;
    uint8_t have_big       = 0;
};

constexpr size_t   EVAL_CACHE_BITS = 13;  // 8192 entries
constexpr size_t   EVAL_CACHE_SIZE = (1u << EVAL_CACHE_BITS);
constexpr uint64_t EVAL_CACHE_MASK = EVAL_CACHE_SIZE - 1;

// thread_local so each search thread has independent cache (no locks)
thread_local EvalCacheEntry eval_cache[EVAL_CACHE_SIZE];
#else
// Disabled or minimal cache: single-entry per-thread stub.
// This keeps the code paths simple and avoids conditional compilation throughout.
struct EvalCacheEntry {
    uint64_t key              = 0;
    int32_t  last_optimism    = 0;
    Value    v                = 0;
    Value    psqt_small       = 0;
    Value    positional_small = 0;
    uint8_t  have_small       = 0;
    Value    psqt_big         = 0;
    Value    positional_big   = 0;
    uint8_t  have_big         = 0;
};

[[maybe_unused]] constexpr size_t EVAL_CACHE_BITS = 0;
constexpr size_t                  EVAL_CACHE_SIZE = 1;
constexpr uint64_t                EVAL_CACHE_MASK = 0;

thread_local EvalCacheEntry eval_cache[EVAL_CACHE_SIZE];
#endif
}

namespace {

// Basic positional cues used to slightly bias evaluation depending on the
// phase of the game. The intent is to provide a light-weight and easily
// switchable mechanism to alter style without replicating any particular
// engine's approach.
struct StyleIndicators {
    int pressure;  // attackers on the opposing king
    int shield;    // defenders near our own king
    int center;    // friendly pieces controlling the central squares
};

// Forward declaration of the internal implementation so tests can reuse it without
// triggering recursion through gather_indicators().
StyleIndicators compute_indicators(const Position& pos);

#ifndef NDEBUG
void assert_indicator_counts();
#endif

// Collect the above indicators from the current position.
StyleIndicators gather_indicators(const Position& pos) {
#ifndef NDEBUG
    static std::once_flag selfTestFlag;
    std::call_once(selfTestFlag, []() { assert_indicator_counts(); });
#endif
    return compute_indicators(pos);
}

StyleIndicators compute_indicators(const Position& pos) {
    StyleIndicators ind{};
    const Square    enemyKing      = pos.square<KING>(~pos.side_to_move());
    const Square    ownKing        = pos.square<KING>(pos.side_to_move());
    const Bitboard  friendlyPieces = pos.pieces(pos.side_to_move());
    [[maybe_unused]] const Bitboard enemyPieces = pos.pieces(~pos.side_to_move());

    ind.pressure = popcount(pos.attackers_to(enemyKing) & friendlyPieces);
    ind.shield   = popcount(pos.attackers_to(ownKing) & friendlyPieces);

    const Bitboard centerBB = square_bb(SQ_D4) | square_bb(SQ_E4) | square_bb(SQ_D5) | square_bb(SQ_E5);
    ind.center               = popcount(friendlyPieces & centerBB);
    return ind;
}

#ifndef NDEBUG
void assert_indicator_counts() {
    auto indicators_for = [](const std::string& fen) {
        StateInfo st;
        Position  pos;
        pos.set(fen, false, &st);
        return compute_indicators(pos);
    };

    {
        // White to move with both sides influencing the black king. Only white attackers
        // should contribute to pressure.
        const StyleIndicators ind = indicators_for("4k3/6n1/6B1/8/8/8/8/4K3 w - - 0 1");
        assert(ind.pressure == 1);
        assert(ind.shield == 0);
    }

    {
        // White king defended by a friendly knight while under attack from an enemy one.
        const StyleIndicators ind = indicators_for("4k3/8/8/8/8/5N2/6n1/4K3 w - - 0 1");
        assert(ind.shield == 1);
        assert(ind.pressure == 0);
    }

    {
        // Black to move: ensure we count black attackers on the white king and black
        // defenders around their own king while ignoring white contributions.
        const StyleIndicators ind = indicators_for("4k3/6n1/6B1/8/8/8/4r1N1/4K3 b - - 0 1");
        assert(ind.pressure == 1);
        assert(ind.shield == 1);
    }
}
#endif

// Compute an adjustment based on the indicators and the current evaluation.
Value adaptive_style_bonus(const Position& pos, Value current) {
    StyleIndicators ind = gather_indicators(pos);

    int atkW = current > 50 ? 3 : 1;
    int defW = current < -50 ? 3 : 1;
    int balW = 2;

    int bonus = atkW * ind.pressure + balW * ind.center - defW * ind.shield;
    return Value(bonus);
}

Bitboard kingside_shield_mask(Color side) {
    return side == Color::WHITE
             ? square_bb(SQ_F2) | square_bb(SQ_G2) | square_bb(SQ_H2)
                 | square_bb(SQ_F3) | square_bb(SQ_G3) | square_bb(SQ_H3)
             : square_bb(SQ_F7) | square_bb(SQ_G7) | square_bb(SQ_H7)
                 | square_bb(SQ_F6) | square_bb(SQ_G6) | square_bb(SQ_H6);
}

Bitboard central_anchor_mask(Color side) {
    return side == Color::WHITE ? square_bb(SQ_D4) | square_bb(SQ_E4)
                                : square_bb(SQ_D5) | square_bb(SQ_E5);
}

int kingside_overextension_penalty(const Position& pos, Color side) {
    const Square kingSq = pos.square<KING>(side);
    const Rank   home   = side == Color::WHITE ? RANK_1 : RANK_8;

    if (rank_of(kingSq) != home || file_of(kingSq) < FILE_G)
        return 0;

    const Bitboard pawns   = pos.pieces(side, PAWN);
    int             pushed  = 0;
    int             deep    = 0;

    for (File file : {FILE_G, FILE_H})
    {
        const Bitboard filePawns = pawns & file_bb(file);
        if (!filePawns)
            continue;

        const Square pawnSq  = lsb(filePawns);
        const int    relRank = static_cast<int>(relative_rank(side, pawnSq));

        if (relRank >= static_cast<int>(RANK_4))
        {
            ++pushed;
            if (relRank >= static_cast<int>(RANK_5))
                ++deep;
        }
    }

    if (!pushed)
        return 0;

    const int shieldPieces = popcount(pos.pieces(side) & kingside_shield_mask(side));
    const int centerPawns  = popcount(pos.pieces(side, PAWN) & central_anchor_mask(side));

    int penalty = 10 * pushed;

    if (shieldPieces <= 1)
        penalty += 6 * pushed;

    if (deep)
        penalty += 4 * deep;

    const Square front1 = side == Color::WHITE ? SQ_G3 : SQ_G6;
    const Square front2 = side == Color::WHITE ? SQ_H3 : SQ_H6;

    int defendedFront = 0;
    for (Square sq : {front1, front2})
        if (is_ok(sq) && (pos.attackers_to(sq) & pos.pieces(side)))
            ++defendedFront;

    if (!defendedFront)
        penalty += 5;

    if (centerPawns == 0)
    {
        penalty += 6 + 3 * pushed;
        const Bitboard enemyCentral = pos.pieces(~side, PAWN)
                                      & central_anchor_mask(~side);
        if (enemyCentral)
            penalty += 4;
    }

    return penalty;
}

Bitboard forward_passed_mask(Color side, Square sq) {
    Bitboard mask   = 0;
    const int step  = side == Color::WHITE ? 1 : -1;
    const int baseR = static_cast<int>(rank_of(sq));
    const int baseF = static_cast<int>(file_of(sq));

    for (int df = -1; df <= 1; ++df)
    {
        const int nf = baseF + df;
        if (nf < static_cast<int>(FILE_A) || nf > static_cast<int>(FILE_H))
            continue;

        for (int r = baseR + step; r >= static_cast<int>(RANK_1)
                                 && r <= static_cast<int>(RANK_8); r += step)
            mask |= square_bb(make_square(static_cast<File>(nf), static_cast<Rank>(r)));
    }

    return mask;
}

inline int manhattan_distance(Square a, Square b) {
    return distance<File>(a, b) + distance<Rank>(a, b);
}

int passed_pawn_pressure(const Position& pos, Color defender) {
    const Color   attacker    = ~defender;
    Bitboard      enemyPawns  = pos.pieces(defender, PAWN);
    Bitboard      passerPawns = pos.pieces(attacker, PAWN);
    const Square  kingSq      = pos.square<KING>(defender);
    const Square  attackerKing = pos.square<KING>(attacker);
    const int     kingPressure = popcount(pos.attackers_to(kingSq) & pos.pieces(attacker));
    int           penalty      = 0;

    while (passerPawns)
    {
        const Square sq = pop_lsb(passerPawns);

        if (enemyPawns & forward_passed_mask(attacker, sq))
            continue;

        const int relRank = static_cast<int>(relative_rank(attacker, sq));
        if (relRank < static_cast<int>(RANK_5))
            continue;

        int base = 8 + 4 * (relRank - static_cast<int>(RANK_5));
        const Square pushSq = sq + pawn_push(attacker);

        if (is_ok(pushSq))
        {
            if (!(pos.attackers_to(pushSq) & pos.pieces(defender)))
                base += 6;

            if (pos.attackers_to(pushSq) & pos.pieces(attacker))
                base += 3;
        }

        const Square targetSq = is_ok(pushSq) ? pushSq : sq;
        const int    kingDist = distance(kingSq, targetSq);
        const int    defenderManhattan = manhattan_distance(kingSq, targetSq);
        const int    attackerManhattan = manhattan_distance(attackerKing, targetSq);

        if (kingDist >= 4)
            base += 4;
        if (kingDist >= 5)
            base += 3;

        if (attackerManhattan <= 3)
            base += 5;
        if (attackerManhattan <= 2)
            base += 4;

        if (defenderManhattan > attackerManhattan)
            base += 3 + defenderManhattan - attackerManhattan;

        if (kingPressure >= 2)
            base += 3;
        if (kingPressure >= 3)
            base += 2;

        penalty += base;
    }

    return penalty;
}

int knight_mobility_penalty(const Position& pos, Color side) {
    Bitboard knights        = pos.pieces(side, KNIGHT);
    const Bitboard friends  = pos.pieces(side);
    const Bitboard enemyPAs = side == Color::WHITE
                                ? pawn_attacks_bb<Color::BLACK>(pos.pieces(Color::BLACK, PAWN))
                                : pawn_attacks_bb<Color::WHITE>(pos.pieces(Color::WHITE, PAWN));

    int penalty = 0;

    while (knights)
    {
        const Square sq       = pop_lsb(knights);
        const Bitboard moves  = attacks_bb<KNIGHT>(sq) & ~friends;
        const int      mob    = popcount(moves);
        const Bitboard safe   = moves & ~enemyPAs;
        const int      safeMb = popcount(safe);

        const File file = file_of(sq);
        const Rank rank = rank_of(sq);

        const bool onEdgeFile = file == FILE_A || file == FILE_H;
        const bool onEdgeRank = rank == RANK_1 || rank == RANK_8;

        if (onEdgeFile || onEdgeRank)
        {
            penalty += 7;
            if (onEdgeFile && onEdgeRank)
                penalty += 4;
        }

        if (mob <= 2)
            penalty += (3 - mob) * 6;

        if (safeMb == 0)
            penalty += 12;

        if (mob == 0)
            penalty += 24;

        if ((onEdgeFile || onEdgeRank) && safeMb == 0)
            penalty += 8;
    }

    return penalty;
}

int knight_outpost_penalty(const Position& pos, Color side) {
    constexpr int    OutpostPenalty = 9;
    const Bitboard   centralSquares = square_bb(SQ_D4) | square_bb(SQ_E4)
                                    | square_bb(SQ_D5) | square_bb(SQ_E5);
    Bitboard         knights        = pos.pieces(side, KNIGHT);
    int              penalty        = 0;

    while (knights)
    {
        const Square sq = pop_lsb(knights);

        if (centralSquares & square_bb(sq))
            continue;

        const Bitboard moves = attacks_bb<KNIGHT>(sq);
        if (moves & centralSquares)
            continue;

        penalty += OutpostPenalty;

        Bitboard reach    = 0;
        Bitboard frontier = moves;
        for (int step = 0; step < 2 && frontier; ++step)
        {
            reach |= frontier;
            Bitboard next = 0;
            Bitboard tmp  = frontier;
            while (tmp)
            {
                const Square fsq = pop_lsb(tmp);
                next |= attacks_bb<KNIGHT>(fsq);
            }
            frontier = next & ~reach;
        }

        if (!(reach & centralSquares))
            penalty += OutpostPenalty / 2;
    }

    return penalty;
}

int central_stability_bonus(const Position& pos, Color side) {
    const Bitboard centerCore = square_bb(SQ_D4) | square_bb(SQ_E4)
                              | square_bb(SQ_D5) | square_bb(SQ_E5);

    Bitboard minors = pos.pieces(side, KNIGHT, BISHOP);
    int      bonus  = 0;

    while (minors)
    {
        const Square sq = pop_lsb(minors);

        if (!(centerCore & square_bb(sq)))
            continue;

        const Bitboard attackers = pos.attackers_to(sq) & pos.pieces(side);
        const bool      pawnSupport
            = bool(attackers & pos.pieces(side, PAWN));
        const Bitboard enemyPawnAttacks
            = side == Color::WHITE
                  ? pawn_attacks_bb<Color::BLACK>(pos.pieces(Color::BLACK, PAWN))
                  : pawn_attacks_bb<Color::WHITE>(pos.pieces(Color::WHITE, PAWN));

        if (enemyPawnAttacks & square_bb(sq))
            continue;

        if (!attackers)
            continue;

        bonus += pawnSupport ? 14 : 9;

        if (pos.pieces(side, KNIGHT) & square_bb(sq))
            bonus += 4;
    }

    return bonus;
}

bool is_passed_pawn(const Position& pos, Color side, Square sq) {
    const Bitboard enemyPawns = pos.pieces(~side, PAWN);
    return !(forward_passed_mask(side, sq) & enemyPawns);
}

int king_pawn_endgame_score(const Position& pos, Color side) {
    if (pos.non_pawn_material(Color::WHITE) != 0 || pos.non_pawn_material(Color::BLACK) != 0)
        return 0;

    Bitboard pawns = pos.pieces(side, PAWN);
    if (!pawns && !pos.pieces(~side, PAWN))
    {
        // King vs king: encourage opposition only.
        const Square ownKing   = pos.square<KING>(side);
        const Square enemyKing = pos.square<KING>(~side);
        const int    dist      = distance(ownKing, enemyKing);
        int          score     = 0;

        if ((distance<File>(ownKing, enemyKing) == 0 || distance<Rank>(ownKing, enemyKing) == 0)
            && dist == 2)
        {
            score += pos.side_to_move() == side ? -6 : 6;
        }

        return score;
    }

    const Square ownKing   = pos.square<KING>(side);
    const Square enemyKing = pos.square<KING>(~side);

    int score = 0;

    Bitboard tmp = pawns;
    while (tmp)
    {
        const Square sq = pop_lsb(tmp);

        if (!is_passed_pawn(pos, side, sq))
            continue;

        const int relRank = static_cast<int>(relative_rank(side, sq));
        const Square promo
            = make_square(file_of(sq), side == Color::WHITE ? RANK_8 : RANK_1);
        const Square front = sq + pawn_push(side);
        const int    ownPromoDist   = distance(ownKing, promo);
        const int    enemyPromoDist = distance(enemyKing, promo);
        const int    ownFrontDist   = is_ok(front) ? distance(ownKing, front) : 0;
        const int    enemyFrontDist = is_ok(front) ? distance(enemyKing, front) : 0;

        score += 12 + 4 * std::max(0, relRank - static_cast<int>(RANK_3));

        if (ownPromoDist + (pos.side_to_move() == side ? 0 : 1) <= enemyPromoDist)
            score += 10;
        else if (ownPromoDist > enemyPromoDist + 1)
            score -= 8;

        if (is_ok(front))
        {
            if (ownFrontDist <= enemyFrontDist - 1)
                score += 6;
            else if (ownFrontDist > enemyFrontDist)
                score -= 4;
        }

        const int enemyKingToPawn = distance(enemyKing, sq);
        if (enemyKingToPawn <= 2)
            score -= 6;
        else if (enemyKingToPawn >= 4)
            score += 4;
    }

    Bitboard enemyPawns = pos.pieces(~side, PAWN);
    while (enemyPawns)
    {
        const Square sq = pop_lsb(enemyPawns);

        if (!is_passed_pawn(pos, ~side, sq))
            continue;

        const Square blockSq = sq + pawn_push(~side);
        const int    blockDist
            = distance(ownKing, is_ok(blockSq) ? blockSq : sq);

        if (blockDist >= 4)
            score -= 8;
        else if (blockDist <= 2)
            score += 4;

        const int relRank = static_cast<int>(relative_rank(~side, sq));
        if (relRank >= static_cast<int>(RANK_5) && blockDist >= 3)
            score -= 6;
    }

    const int kingSep = distance(ownKing, enemyKing);
    if ((distance<File>(ownKing, enemyKing) == 0 || distance<Rank>(ownKing, enemyKing) == 0)
        && kingSep == 2)
    {
        score += pos.side_to_move() == side ? -6 : 6;
    }

    return score;
}

// Simple tempo bonus favoring the side to move.
//
// The evaluation pipeline assumes this term is anti-symmetric under a
// color flip (see tests/eval_symmetry_test.cpp). Returning a constant value
// regardless of the side to move breaks that expectation and lets the eval
// drift by 20cp between mirrored positions, which in turn regresses search
// decisions. Keep the classical side-to-move orientation until the rest of
// the search is updated to consume a symmetric tempo term.
Value tempo_bonus(const Position& pos) {
    constexpr Value Tempo = Value(10);
    return pos.side_to_move() == Color::WHITE ? Tempo : -Tempo;
}

}  // namespace

namespace Eval {
static bool adaptive_style = false;
void        set_adaptive_style(bool enabled) { adaptive_style = enabled; }
}  // namespace Eval

// Returns a static, purely materialistic evaluation of the position from
// the point of view of the side to move. It can be divided by PawnValue to get
// an approximation of the material advantage on the board in terms of pawns.
int Eval::simple_eval(const Position& pos) {
    Color c = pos.side_to_move();
    return PawnValue * (pos.count<PAWN>(c) - pos.count<PAWN>(~c))
         + (pos.non_pawn_material(c) - pos.non_pawn_material(~c));
}

inline bool Eval::use_smallnet(const Position& pos) { return std::abs(simple_eval(pos)) > 962; }

// Evaluate is the evaluator for the outer world. It returns a static evaluation
// of the position from the point of view of the side to move.
Value Eval::evaluate(const Eval::NNUE::Networks&    networks,
                     const Position&                pos,
                     Eval::NNUE::AccumulatorStack&  accumulators,
                     Eval::NNUE::AccumulatorCaches& caches,
                     int                            optimism) {

    assert(!pos.checkers());

    const uint64_t  posKey = pos.key();
    const uint64_t  idx    = posKey & EVAL_CACHE_MASK;  // index by position key only
    EvalCacheEntry& e      = eval_cache[idx];

    const int requestedOptimism = optimism;

    // If the stored entry refers to a different position, invalidate its NNUE flags.
    if (e.key != posKey)
    {
        e.key           = posKey;
        e.have_small    = 0;
        e.have_big      = 0;
        e.last_optimism = 0;
        // `v` can remain stale until we compute and overwrite it.
    }
    else
    {
        // If exact same optimism already computed for this position, return final cached v.
        if (e.last_optimism == requestedOptimism)
            return e.v;
    }

    bool  smallNet = use_smallnet(pos);
    Value psqt = 0, positional = 0;

    if (smallNet)
    {
        if (e.have_small)
        {
            psqt       = e.psqt_small;
            positional = e.positional_small;
        }
        else
        {
            std::tie(psqt, positional) = networks.small.evaluate(pos, accumulators, &caches.small);
            e.psqt_small               = psqt;
            e.positional_small         = positional;
            e.have_small               = 1;
        }
    }
    else
    {
        if (e.have_big)
        {
            psqt       = e.psqt_big;
            positional = e.positional_big;
        }
        else
        {
            std::tie(psqt, positional) = networks.big.evaluate(pos, accumulators, &caches.big);
            e.psqt_big                 = psqt;
            e.positional_big           = positional;
            e.have_big                 = 1;
        }
    }

    Value nnue = (125 * psqt + 131 * positional) / 128;

    // Re-evaluate the position when higher eval accuracy is worth the time spent
    if (smallNet && (std::abs(nnue) < 236))
    {
        // Try to use cached big-network outputs if present, otherwise compute and store.
        if (e.have_big)
        {
            psqt       = e.psqt_big;
            positional = e.positional_big;
        }
        else
        {
            std::tie(psqt, positional) = networks.big.evaluate(pos, accumulators, &caches.big);
            e.psqt_big                 = psqt;
            e.positional_big           = positional;
            e.have_big                 = 1;
        }
        nnue     = (125 * psqt + 131 * positional) / 128;
        smallNet = false;
    }

    // Blend optimism and eval with nnue complexity
    const int nnueComplexity = std::abs(psqt - positional);
    int       scaledOptimism = requestedOptimism;
    if (scaledOptimism != 0)
        scaledOptimism += scaledOptimism * nnueComplexity / 468;
    if (nnue != 0)
        nnue -= nnue * nnueComplexity / 18000;

    const int material       = 535 * pos.count<PAWN>() + pos.non_pawn_material();
    const int numerator_nnue = nnue * (77777 + material);
    const int numerator_opt  = scaledOptimism * (7777 + material);
    int       v              = (numerator_nnue + numerator_opt) / 77777;

    // Damp down the evaluation linearly when shuffling
    v -= v * pos.rule50_count() / 212;

    v += tempo_bonus(pos);

    const int flankPenaltyWhite = kingside_overextension_penalty(pos, Color::WHITE);
    const int flankPenaltyBlack = kingside_overextension_penalty(pos, Color::BLACK);
    const int passerPressureWhite = passed_pawn_pressure(pos, Color::WHITE);
    const int passerPressureBlack = passed_pawn_pressure(pos, Color::BLACK);
    const int knightPenaltyWhite  = knight_mobility_penalty(pos, Color::WHITE);
    const int knightPenaltyBlack  = knight_mobility_penalty(pos, Color::BLACK);
    const int outpostPenaltyWhite = knight_outpost_penalty(pos, Color::WHITE);
    const int outpostPenaltyBlack = knight_outpost_penalty(pos, Color::BLACK);
    const int centralBonusWhite   = central_stability_bonus(pos, Color::WHITE);
    const int centralBonusBlack   = central_stability_bonus(pos, Color::BLACK);
    const int kpScoreWhite        = king_pawn_endgame_score(pos, Color::WHITE);
    const int kpScoreBlack        = king_pawn_endgame_score(pos, Color::BLACK);

    v += flankPenaltyBlack - flankPenaltyWhite;
    v += passerPressureBlack - passerPressureWhite;
    v += knightPenaltyBlack - knightPenaltyWhite;
    v += outpostPenaltyBlack - outpostPenaltyWhite;
    v += centralBonusWhite - centralBonusBlack;
    v += kpScoreWhite - kpScoreBlack;

    // Guarantee evaluation does not hit the tablebase range
    v = std::clamp(v, VALUE_TB_LOSS_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);

    // Optional style-based tweak which slightly biases the score depending on
    // basic positional indicators. This does not aim to implement any specific
    // style but merely demonstrates how evaluation terms can be combined.
    if (adaptive_style)
    {
        v += adaptive_style_bonus(pos, Value(v));
        v = std::clamp(v, VALUE_TB_LOSS_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);
    }

    // Store result and the optimism used for the final `v`.
    e.last_optimism = requestedOptimism;
    e.v             = v;

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

    std::stringstream ss;
    ss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2);
    ss << '\n' << NNUE::trace(pos, networks, *caches) << '\n';

    ss << std::showpoint << std::showpos << std::fixed << std::setprecision(2) << std::setw(15);

    // Try to reuse cached big-network outputs if present to avoid recomputation.
    const uint64_t  posKey = pos.key();
    const uint64_t  idx    = posKey & EVAL_CACHE_MASK;
    EvalCacheEntry& e      = eval_cache[idx];
    Value           psqt = 0, positional = 0;
    if (e.key == posKey && e.have_big)
    {
        psqt       = e.psqt_big;
        positional = e.positional_big;
    }
    else
    {
        std::tie(psqt, positional) = networks.big.evaluate(pos, accumulators, &caches->big);
        // store in cache (overwrite entry if different key)
        if (e.key != posKey)
        {
            e.key           = posKey;
            e.have_small    = 0;
            e.have_big      = 0;
            e.last_optimism = 0;
        }
        e.psqt_big       = psqt;
        e.positional_big = positional;
        e.have_big       = 1;
    }
    Value v = psqt + positional;
    v       = pos.side_to_move() == Color::WHITE ? v : -v;
    ss << "NNUE evaluation        " << 0.01 * UCIEngine::to_cp(v, pos) << " (white side)\n";

    v = evaluate(networks, pos, accumulators, *caches, VALUE_ZERO);
    v = pos.side_to_move() == Color::WHITE ? v : -v;
    ss << "Final evaluation       " << 0.01 * UCIEngine::to_cp(v, pos) << " (white side)";
    ss << " [with scaled NNUE, ...]";
    ss << "\n";

    return ss.str();
}

}  // namespace Stockfish
