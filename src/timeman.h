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

#include <array>
#include <cstdint>
#include <vector>

#include "misc.h"

namespace Stockfish {

class OptionsMap;
enum class Color : int8_t;

namespace Search {
struct LimitsType;
}

// The TimeManagement class computes the optimal time to think depending on
// the maximum available time, the game move number, and other parameters.
class TimeManagement {
   public:
    void init(Search::LimitsType& limits,
              Color               us,
              int                 ply,
              const OptionsMap&   options,
              double&             originalTimeAdjust);

    TimePoint optimum() const;
    TimePoint maximum() const;
    template<typename FUNC>
    TimePoint elapsed(FUNC nodes) const {
        return useNodesTime ? TimePoint(nodes()) : elapsed_time();
    }
    TimePoint elapsed_time() const { return now() - startTime; };

    void clear();
    void advance_nodes_time(std::int64_t nodes);
    void record_time_usage(TimePoint spent, bool quietPhase);
    std::vector<TimePoint> recent_time_samples() const;
    std::vector<TimePoint> recent_optimum_samples() const;

   private:
    double    usage_ratio() const;
    double    quiet_phase_bias() const;
    TimePoint startTime;
    TimePoint optimumTime;
    TimePoint maximumTime;

    std::int64_t availableNodes = -1;     // When in 'nodes as time' mode
    bool         useNodesTime   = false;  // True if we are in 'nodes as time' mode

    static constexpr size_t HistorySize = 32;
    std::array<TimePoint, HistorySize> spentHistory{};
    std::array<TimePoint, HistorySize> optimumHistory{};
    std::array<bool, HistorySize>      quietHistory{};
    size_t                             historyCount    = 0;
    size_t                             historyIndex    = 0;
    bool                               pendingSample   = false;
    TimePoint                          pendingOptimum  = 0;
}; 

}  // namespace Stockfish

