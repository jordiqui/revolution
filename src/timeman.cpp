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

namespace Stockfish {

TimeModel GTime;

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
void TimeManagement::init(Search::LimitsType& limits,
                          Color               us,
                          int                 ply,
                          const OptionsMap&   options,
                          double&             originalTimeAdjust,
                          int                 evaluationCp) {
    TimePoint npmsec = TimePoint(options["nodestime"]);

    // If we have no time, we don't need to fully initialize TM.
    // startTime is used by movetime and useNodesTime is used in elapsed calls.
    startTime    = limits.startTime;
    useNodesTime = npmsec != 0;

    const int usIdx = static_cast<int>(us);

    if (limits.time[usIdx] == 0)
        return;

    TimePoint moveOverhead =
      GSearch.conservative ? TimePoint(GTime.move_overhead_ms)
                           : TimePoint(options["Move Overhead"]);

    // optScale is a percentage of available time to use for the current move.
    // maxScale is a multiplier applied to optimumTime.
    double optScale, maxScale;

    // If we have to play in 'nodes as time' mode, then convert from time
    // to nodes, and use resulting values in time management formulas.
    // WARNING: to avoid time losses, the given npmsec (nodes per millisecond)
    // must be much lower than the real engine speed.
    if (useNodesTime)
    {
        if (availableNodes == -1)                 // Only once at game start
            availableNodes = npmsec * limits.time[usIdx];  // Time is in msec

        // Convert from milliseconds to nodes
        limits.time[usIdx] = TimePoint(availableNodes);
        limits.inc[usIdx] *= npmsec;
        limits.npmsec = npmsec;
        moveOverhead *= npmsec;
    }

    // These numbers are used where multiplications, divisions or comparisons
    // with constants are involved.
    const int64_t   scaleFactor = useNodesTime ? npmsec : 1;
    const TimePoint scaledTime  = limits.time[usIdx] / scaleFactor;

    // Maximum move horizon
    int centiMTG = limits.movestogo ? std::min(limits.movestogo * 100, 5000) : 5051;

    // If less than one second, gradually reduce mtg
    if (scaledTime < 1000)
        centiMTG = scaledTime * 5.051;

    // Make sure timeLeft is > 0 since we may use it as a divisor
    TimePoint timeLeft =
      std::max(TimePoint(1),
               limits.time[usIdx]
                 + (limits.inc[usIdx] * (centiMTG - 100) - moveOverhead * (200 + centiMTG)) / 100);

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

        optScale =
          std::min(0.0121431 + std::pow(ply + 2.94693, 0.461073) * optConstant,
                   0.213035 * limits.time[usIdx] / timeLeft)
          * originalTimeAdjust;

        maxScale = std::min(6.67704, maxConstant + ply / 11.9847);
    }

    // x moves in y seconds (+ z increment)
    else
    {
        optScale =
          std::min((0.88 + ply / 116.4) / (centiMTG / 100.0), 0.88 * limits.time[usIdx] / timeLeft);
        maxScale = 1.3 + 0.11 * (centiMTG / 100.0);
    }

    // Limit the maximum possible time for this move
    optimumTime = TimePoint(optScale * timeLeft);
    maximumTime =
      TimePoint(std::min(0.825179 * limits.time[usIdx] - moveOverhead, maxScale * optimumTime)) - 10;

    if (GSearch.conservative)
    {
        int64_t time_left_ms = limits.time[usIdx];
        int64_t inc_ms       = limits.inc[usIdx];

        int64_t base         = std::max<int64_t>(GTime.min_think_ms, optimumTime);
        int64_t dyn_overhead = GTime.move_overhead_ms;
        if (inc_ms < 200)
            dyn_overhead += 10;

        int64_t budget = std::max<int64_t>(GTime.min_think_ms, base - dyn_overhead);
        budget =
          std::min<int64_t>(budget,
                            std::max<int64_t>(0, time_left_ms - GTime.panic_margin_ms));
        optimumTime = maximumTime = TimePoint(budget);
    }

    if (us == Color::BLACK && evaluationCp <= -50)
    {
        double factor = options["BlackTimeFactor"] / 100.0;
        optimumTime   = TimePoint(optimumTime * factor);
        maximumTime   = TimePoint(maximumTime * factor);
        maximumTime   = std::max(maximumTime, optimumTime);
    }

    if (options["Ponder"])
        optimumTime += optimumTime / 4;

    TimePoint minimumThinkingTime =
      GSearch.conservative ? TimePoint(GTime.min_think_ms) : TimePoint(0);
    TimePoint safetyBuffer =
      GSearch.conservative ? TimePoint(GTime.panic_margin_ms)
                           : TimePoint(options["Time Buffer"]);
    optimumTime                   = std::max(optimumTime, minimumThinkingTime);
    maximumTime                   = std::max(maximumTime - safetyBuffer, minimumThinkingTime);
}

}  // namespace Stockfish
