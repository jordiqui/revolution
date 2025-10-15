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

#include "timeman.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

#include "search.h"
#include "ucioption.h"

#include "bitboard.h"
#include "evaluate.h"
#include "position.h"

namespace Stockfish {

TimePoint TimeManagement::optimum() const { return optimumTime; }
TimePoint TimeManagement::maximum() const { return maximumTime; }

void TimeManagement::clear() {
    availableNodes = -1;  // When in 'nodes as time' mode
}

void TimeManagement::advance_nodes_time(std::int64_t nodes) {
    assert(useNodesTime);
    availableNodes = std::max(int64_t(0), availableNodes - nodes);
}

// Called at the beginning of the search and calculates
// the bounds of time allowed for the current game ply. We currently support:
//      1) x basetime (+ z increment)
//      2) x moves in y seconds (+ z increment)
namespace {

constexpr Bitboard DarkSquares = 0xAA55AA55AA55AA55ULL;

Bitboard forward_passed_mask(Color side, Square sq) {
    Bitboard mask    = 0;
    const int step   = side == Color::WHITE ? 1 : -1;
    const int baseR  = static_cast<int>(rank_of(sq));
    const int baseF  = static_cast<int>(file_of(sq));

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

double passed_pawn_urgency(const Position& pos, Color side) {
    Bitboard pawns      = pos.pieces(side, PAWN);
    Bitboard enemyPawns = pos.pieces(~side, PAWN);
    double   urgency    = 0.0;

    while (pawns)
    {
        const Square sq = pop_lsb(pawns);

        if (enemyPawns & forward_passed_mask(side, sq))
            continue;

        const int relRank         = static_cast<int>(relative_rank(side, sq));
        const int stepsToPromote  = std::max(0, 7 - relRank);
        const double advancement  = 1.0 - std::min(5, stepsToPromote) / 6.0;
        const double baseUrgency  = 0.12 + advancement * 0.36;
        const double moverFactor  = pos.side_to_move() == side ? 1.15 : 0.95;

        urgency = std::max(urgency, baseUrgency * moverFactor);
    }

    const double materialFactor = 1.0
                                - std::min(1.0, double(pos.non_pawn_material())
                                                    / double(4 * QueenValue));

    return urgency * (0.8 + 0.4 * materialFactor);
}

}  // namespace

void TimeManagement::init(Search::LimitsType& limits,
                          Color               us,
                          int                 ply,
                          const OptionsMap&   options,
                          const Position&     pos,
                          double&             originalTimeAdjust) {
    TimePoint npmsec = TimePoint(options["nodestime"]);

    // If we have no time, we don't need to fully initialize TM.
    // startTime is used by movetime and useNodesTime is used in elapsed calls.
    startTime    = limits.startTime;
    useNodesTime = npmsec != 0;

    if (limits.time[static_cast<int>(us)] == 0)
        return;

    TimePoint moveOverhead = TimePoint(options["Move Overhead"]);
    double    slowMover    = options["Slow Mover"] / 100.0;

    // Adjust time usage heuristics for common time controls
    double baseSeconds = double(limits.time[static_cast<int>(us)]) / 1000.0;
    double incSeconds  = double(limits.inc[static_cast<int>(us)]) / 1000.0;
    if (baseSeconds <= 60 && incSeconds == 0)
        slowMover *= 0.8;  // 60s + 0ms
    else if (baseSeconds <= 180 && incSeconds >= 10)
        slowMover *= 1.05;  // 180s + 10s
    else if (baseSeconds >= 960 && incSeconds == 0)
        slowMover *= 1.15;  // 960s + 0ms

    const double movesToGoEstimate = limits.movestogo ? limits.movestogo : 40.0;
    const double avgMoveBudget =
      (baseSeconds + incSeconds * movesToGoEstimate) / std::max(1.0, movesToGoEstimate);
    const double matchBias = std::clamp(avgMoveBudget / 3.0, 0.6, 1.6);

    slowMover *= 0.94 + 0.08 * matchBias;

    const double overheadBias = avgMoveBudget < 1.25 ? 1.18
                                  : avgMoveBudget > 5.5 ? 0.93
                                                        : 1.02 + 0.04 * (matchBias - 1.0);
    moveOverhead = TimePoint(std::lround(std::max(0.0, double(moveOverhead) * overheadBias)));

    const Eval::StyleIndicators ourIndicators   = Eval::style_indicators(pos, us);
    const Eval::StyleIndicators theirIndicators = Eval::style_indicators(pos, ~us);
    const double                ourPasserUrgency   = passed_pawn_urgency(pos, us);
    const double                theirPasserUrgency = passed_pawn_urgency(pos, ~us);

    const double shieldBalance = double(ourIndicators.shield) - double(theirIndicators.pressure);
    const double calmScore     = std::max(0.0, shieldBalance - theirPasserUrgency * 0.75);
    double       dangerScore   = std::max(0.0, -shieldBalance)
                           + std::max(0.0, theirPasserUrgency - ourPasserUrgency * 0.5);

    if (!(pos.pieces(us, BISHOP) & DarkSquares) && ourIndicators.shield <= 1)
        dangerScore += 0.8;

    const double dangerScale = std::clamp(dangerScore * 0.06, 0.0, 0.20);
    if (dangerScale > 0.0)
    {
        slowMover *= std::max(0.72, 1.0 - dangerScale);
        const double overheadBoost = 1.0 + 1.8 * dangerScale;
        moveOverhead = TimePoint(std::lround(std::max(0.0, double(moveOverhead) * overheadBoost)));
    }
    else
    {
        const double calmScale = std::min(0.12, calmScore * 0.04);
        if (calmScale > 0.0)
            slowMover *= 1.0 + calmScale;
    }

    // optScale is a percentage of available time to use for the current move.
    // maxScale is a multiplier applied to optimumTime.
    double optScale, maxScale;

    // If we have to play in 'nodes as time' mode, then convert from time
    // to nodes, and use resulting values in time management formulas.
    // WARNING: to avoid time losses, the given npmsec (nodes per millisecond)
    // must be much lower than the real engine speed.
    if (useNodesTime)
    {
        if (availableNodes == -1)                       // Only once at game start
            availableNodes = npmsec * limits.time[static_cast<int>(us)];  // Time is in msec

        // Convert from milliseconds to nodes
        limits.time[static_cast<int>(us)] = TimePoint(availableNodes);
        limits.inc[static_cast<int>(us)] *= npmsec;
        limits.npmsec = npmsec;
        moveOverhead *= npmsec;
    }

    // These numbers are used where multiplications, divisions or comparisons
    // with constants are involved.
    const int64_t   scaleFactor = useNodesTime ? npmsec : 1;
    const TimePoint scaledTime  = limits.time[static_cast<int>(us)] / scaleFactor;

    // Maximum move horizon
    int centiMTG = limits.movestogo ? std::min(limits.movestogo * 100, 5000) : 5051;

    // If less than one second, gradually reduce mtg
    if (scaledTime < 1000)
        centiMTG = scaledTime * 5.051;

    // Make sure timeLeft is > 0 since we may use it as a divisor
    TimePoint timeLeft =
      std::max(TimePoint(1),
               limits.time[static_cast<int>(us)]
                 + (limits.inc[static_cast<int>(us)] * (centiMTG - 100) - moveOverhead * (200 + centiMTG)) / 100);

    // x basetime (+ z increment)
    // If there is a healthy increment, timeLeft can exceed the actual available
    // game time for the current move, so also cap to a percentage of available game time.
    if (limits.movestogo == 0)
    {
        // Extra time according to timeLeft
        if (originalTimeAdjust < 0)
            originalTimeAdjust = 0.3128 * std::log10(timeLeft) - 0.4354;

        // Calculate time constants based on current time left.
        double logTimeInSec = std::log10(scaledTime / 1000.0);
        double optConstant  = std::min(0.0032116 + 0.000321123 * logTimeInSec, 0.00508017);
        double maxConstant  = std::max(3.3977 + 3.03950 * logTimeInSec, 2.94761);

        optScale = std::min(0.0121431 + std::pow(ply + 2.94693, 0.461073) * optConstant,
                            0.213035 * limits.time[static_cast<int>(us)] / timeLeft)
                 * originalTimeAdjust;

        maxScale = std::min(6.67704, maxConstant + ply / 11.9847);
    }

    // x moves in y seconds (+ z increment)
    else
    {
        optScale =
          std::min((0.88 + ply / 116.4) / (centiMTG / 100.0),
                   0.88 * limits.time[static_cast<int>(us)] / timeLeft);
        maxScale = 1.3 + 0.11 * (centiMTG / 100.0);
    }

    optScale *= slowMover;

    double passerTimeScale = 1.0;

    if (pos.count<PAWN>() <= 8)
    {
        const double combined = std::clamp(ourPasserUrgency + 0.6 * theirPasserUrgency, 0.0, 0.24);

        passerTimeScale += combined;
    }

    optScale *= passerTimeScale;
    maxScale *= 1.0 + 0.75 * std::max(0.0, passerTimeScale - 1.0);

    // Limit the maximum possible time for this move
    optimumTime = TimePoint(optScale * timeLeft);
    maximumTime =
      TimePoint(std::min(0.90 * limits.time[static_cast<int>(us)] - moveOverhead,
                          maxScale * optimumTime)) - 10;

    if ((bool) options["Revolution Conservative Search"])
    {
        // Clamp the time budget to keep some safety margin. Use an adaptive
        // overhead that increases with the remaining time, providing a
        // slightly larger buffer in long time controls while still being
        // conservative for quick controls.
        TimePoint adaptiveOverhead =
          moveOverhead + TimePoint(options["Time Buffer"]) + limits.time[static_cast<int>(us)] / 30;
        TimePoint maxBudget =
          std::max(TimePoint(1), limits.time[static_cast<int>(us)] - adaptiveOverhead);
        optimumTime = std::min(optimumTime, maxBudget);
        maximumTime = std::min(maximumTime, maxBudget);
    }

    if (options["Ponder"])
        optimumTime += optimumTime / 4;

    TimePoint minimumThinkingTime = TimePoint(options["Minimum Thinking Time"]);
    TimePoint safetyBuffer        = TimePoint(options["Time Buffer"]);

    if (scaledTime > 1500)
    {
        const TimePoint desiredMinimum = TimePoint(std::min<int64_t>(
          static_cast<int64_t>(timeLeft), 90 * scaleFactor));
        minimumThinkingTime = std::max(minimumThinkingTime, desiredMinimum);
    }

    optimumTime = std::max(optimumTime, minimumThinkingTime);
    maximumTime = std::max(maximumTime - safetyBuffer, minimumThinkingTime);
}

}  // namespace Stockfish
