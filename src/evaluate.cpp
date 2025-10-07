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
#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

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

constexpr std::array<Square, 4> CentralSquares = {SQ_D4, SQ_E4, SQ_D5, SQ_E5};

Eval::ManualEvalWeights manualWeights;

Bitboard forward_rays(Color c, Square sq) {
    Bitboard   mask = 0;
    Direction  dir  = pawn_push(c);
    Square     s    = sq;
    const auto step = [&]() {
        s += dir;
        return is_ok(s);
    };

    while (step())
        mask |= square_bb(s);

    return mask;
}

Bitboard passed_span(Color c, Square sq) {
    Bitboard front = forward_rays(c, sq);
    Bitboard span  = front;

    if (file_of(sq) > FILE_A)
        span |= shift<WEST>(front);
    if (file_of(sq) < FILE_H)
        span |= shift<EAST>(front);

    return span;
}

bool is_passed_pawn(const Position& pos, Color c, Square sq) {
    return !(passed_span(c, sq) & pos.pieces(~c, PAWN));
}

Bitboard king_shield_mask(Color c, Square king) {
    Bitboard firstRing = shift(pawn_push(c), square_bb(king));
    Bitboard mask      = firstRing;
    mask |= shift<EAST>(firstRing);
    mask |= shift<WEST>(firstRing);
    Bitboard secondRing = shift(pawn_push(c), firstRing);
    mask |= secondRing;
    return mask;
}

int king_safety_contrib(const Position& pos, Color c) {
    const Square    king         = pos.square<KING>(c);
    const Bitboard  pawns        = pos.pieces(c, PAWN);
    const Bitboard  enemyPieces  = pos.pieces(~c);
    const Bitboard  enemySliders = pos.pieces(~c, ROOK) | pos.pieces(~c, QUEEN);
    int             penalty      = 0;

    Bitboard shieldMask = king_shield_mask(c, king);
    int      shieldCnt  = popcount(pawns & shieldMask);
    if (shieldCnt < 2)
        penalty += (2 - shieldCnt) * 10;

    Bitboard fileMask = file_bb(king);
    File     kFile    = file_of(king);
    if (kFile > FILE_A)
        fileMask |= file_bb(File(int(kFile) - 1));
    if (kFile < FILE_H)
        fileMask |= file_bb(File(int(kFile) + 1));

    Bitboard pawnsAround = pawns & fileMask;
    Bitboard advanced    = pawnsAround;
    while (advanced)
    {
        Square sq     = pop_lsb(advanced);
        int    rel    = static_cast<int>(relative_rank(c, sq));
        int    excess = std::max(0, rel - static_cast<int>(RANK_3));
        if (excess)
            penalty += 6 * excess;
    }

    Bitboard forward = forward_rays(c, king) & file_bb(king);
    if (!(forward & pawns))
        penalty += 18;

    Bitboard slidersOnFile = enemySliders & file_bb(king);
    while (slidersOnFile)
    {
        Square slider = pop_lsb(slidersOnFile);
        Bitboard between = between_bb(slider, king);
        if (!(between & pos.pieces()) || !(between & pos.pieces(c)))
        {
            penalty += 12;
            break;
        }
    }

    const std::array<std::pair<Square, Square>, 2> flankPatterns = {
      std::make_pair(SQ_G4, SQ_H4), std::make_pair(SQ_G5, SQ_H6)};
    for (const auto& [s1, s2] : flankPatterns)
    {
        Square rel1 = relative_square(c, s1);
        Square rel2 = relative_square(c, s2);
        if ((pawns & rel1) && (pawns & rel2))
        {
            penalty += 14;
            break;
        }
    }

    // Slight bonus if enemy pieces are far from the king area
    Bitboard kingRing = attacks_bb<KING>(king);
    if (!(kingRing & enemyPieces))
        penalty = std::max(0, penalty - 6);

    return c == Color::WHITE ? -penalty : penalty;
}

int passed_pawn_contrib(const Position& pos, Color c) {
    static constexpr std::array<int, 8> RankBonus = {0, 0, 12, 28, 44, 68, 110, 0};

    Bitboard pawns = pos.pieces(c, PAWN);
    int      score = 0;

    while (pawns)
    {
        Square sq = pop_lsb(pawns);
        if (!is_passed_pawn(pos, c, sq))
            continue;

        int relRank = static_cast<int>(relative_rank(c, sq));
        int bonus   = RankBonus[relRank];

        Square blockSq = sq + pawn_push(c);
        if (is_ok(blockSq))
        {
            Bitboard ourCtrl   = pos.attackers_to(blockSq) & pos.pieces(c);
            Bitboard enemyCtrl = pos.attackers_to(blockSq) & pos.pieces(~c);
            int      diff      = popcount(ourCtrl) - popcount(enemyCtrl);

            if (pos.empty(blockSq))
                bonus += 6 + 2 * relRank + std::max(diff, 0) * 4;
            else if (color_of(pos.piece_on(blockSq)) == c)
                bonus += 4;
            else
            {
                bonus -= 6;
                if (diff > 0)
                    bonus += diff * 5;
                else if (diff < 0)
                    bonus += diff * 6;
            }

            // Penalize the opponent additionally when the block square is under our control
            if (diff > 1)
                bonus += diff * 3;
        }

        // Encourage unstoppable passer when king is far
        Square enemyKing = pos.square<KING>(~c);
        int    kingDist  = distance(enemyKing, sq);
        if (relRank >= static_cast<int>(RANK_6) && kingDist > 3)
            bonus += 18;

        score += (c == Color::WHITE ? bonus : -bonus);
    }

    return score;
}

int minor_mobility_contrib(const Position& pos, Color c) {
    Bitboard knights = pos.pieces(c, KNIGHT);
    int      score   = 0;

    while (knights)
    {
        Square sq       = pop_lsb(knights);
        Bitboard moves  = attacks_bb<KNIGHT>(sq) & ~pos.pieces(c);
        int      mobile = popcount(moves);

        int trappedPenalty = std::max(0, 3 - mobile) * 6;
        if (mobile <= 1)
            trappedPenalty += 10;

        if (!edge_distance(file_of(sq)) || relative_rank(c, sq) <= RANK_2
            || relative_rank(c, sq) >= RANK_7)
            trappedPenalty += 6;

        Bitboard sqBB = square_bb(sq);
        if (sqBB & (CentralSquares[0] | CentralSquares[1] | CentralSquares[2] | CentralSquares[3]))
        {
            int bonus = 14;
            if (pos.attackers_to(sq) & pos.pieces(c, PAWN))
                bonus += 6;
            if (!(pos.attackers_to(sq) & pos.pieces(~c)))
                bonus += 4;
            trappedPenalty -= bonus;
        }

        score += (c == Color::WHITE ? -trappedPenalty : trappedPenalty);
    }

    return score;
}

int passed_pawn_distance_penalty(const Position& pos, Color c, Square king) {
    Bitboard pawns = pos.pieces(c, PAWN);
    int      score = 0;

    while (pawns)
    {
        Square sq = pop_lsb(pawns);
        if (!is_passed_pawn(pos, c, sq))
            continue;

        int dist = distance(king, sq);
        score -= dist * 6;
    }

    return score;
}

int pawn_endgame_contrib(const Position& pos) {
    if (pos.non_pawn_material())
        return 0;

    const Square kingW = pos.square<KING>(Color::WHITE);
    const Square kingB = pos.square<KING>(Color::BLACK);

    auto center_bonus = [](Square king) {
        int df = std::min(std::abs(int(file_of(king)) - int(FILE_D)),
                          std::abs(int(file_of(king)) - int(FILE_E)));
        int dr = std::min(std::abs(int(rank_of(king)) - int(RANK_4)),
                          std::abs(int(rank_of(king)) - int(RANK_5)));
        return 18 - 6 * (df + dr);
    };

    int score = center_bonus(kingW) - center_bonus(kingB);

    score += passed_pawn_distance_penalty(pos, Color::BLACK, kingW);
    score -= passed_pawn_distance_penalty(pos, Color::WHITE, kingB);

    // Reward supporting distance to own passers
    Bitboard whitePassers = pos.pieces(Color::WHITE, PAWN);
    while (whitePassers)
    {
        Square sq = pop_lsb(whitePassers);
        if (!is_passed_pawn(pos, Color::WHITE, sq))
            continue;
        int dist = distance(kingW, sq);
        score += std::max(0, 6 - dist) * 4;
    }

    Bitboard blackPassers = pos.pieces(Color::BLACK, PAWN);
    while (blackPassers)
    {
        Square sq = pop_lsb(blackPassers);
        if (!is_passed_pawn(pos, Color::BLACK, sq))
            continue;
        int dist = distance(kingB, sq);
        score -= std::max(0, 6 - dist) * 4;
    }

    if ((file_of(kingW) == file_of(kingB) || rank_of(kingW) == rank_of(kingB))
        && distance(kingW, kingB) == 2)
        score += pos.side_to_move() == Color::WHITE ? -12 : 12;

    return score;
}

Eval::ManualEvalTerms compute_manual_terms_impl(const Position& pos) {
    Eval::ManualEvalTerms terms;
    terms.kingSafety = king_safety_contrib(pos, Color::WHITE) + king_safety_contrib(pos, Color::BLACK);
    terms.passedPawns = passed_pawn_contrib(pos, Color::WHITE) + passed_pawn_contrib(pos, Color::BLACK);
    terms.minorMobility =
      minor_mobility_contrib(pos, Color::WHITE) + minor_mobility_contrib(pos, Color::BLACK);
    terms.pawnEndgames = pawn_endgame_contrib(pos);
    return terms;
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

    Eval::ManualEvalTerms manualTerms = compute_manual_terms_impl(pos);
    int                   manualScore = manualTerms.kingSafety * manualWeights.kingSafety
                      + manualTerms.passedPawns * manualWeights.passedPawns
                      + manualTerms.minorMobility * manualWeights.minorMobility
                      + manualTerms.pawnEndgames * manualWeights.pawnEndgames;
    int roundedManual = manualScore >= 0 ? manualScore + 50 : manualScore - 50;
    v += Value(roundedManual / 100);

    v += tempo_bonus(pos);

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

Eval::ManualEvalTerms Eval::compute_manual_terms(const Position& pos) {
    return compute_manual_terms_impl(pos);
}

Eval::ManualEvalWeights Eval::manual_eval_weights() { return manualWeights; }

void Eval::set_manual_eval_weights(const ManualEvalWeights& weights) { manualWeights = weights; }

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
