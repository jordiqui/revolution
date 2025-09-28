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

// Simple tempo bonus favoring the side to move
Value tempo_bonus(const Position& pos) {
    return pos.side_to_move() == Color::WHITE ? Value(10) : Value(-10);
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
        if (e.last_optimism == optimism)
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
    if (optimism != 0)
        optimism += optimism * nnueComplexity / 468;
    if (nnue != 0)
        nnue -= nnue * nnueComplexity / 18000;

    const int material       = 535 * pos.count<PAWN>() + pos.non_pawn_material();
    const int numerator_nnue = nnue * (77777 + material);
    const int numerator_opt  = optimism * (7777 + material);
    int       v              = (numerator_nnue + numerator_opt) / 77777;

    // Damp down the evaluation linearly when shuffling
    v -= v * pos.rule50_count() / 212;

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
    e.last_optimism = optimism;
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
