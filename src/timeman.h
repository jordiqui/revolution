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

#include <cstdint>

#include "misc.h"
#include "types.h"

namespace Stockfish {

// Configuration used by the conservative time manager when the
// "Use 040825 Search" option is enabled.
struct TimeModel {
    int64_t min_think_ms     = 30;  // Minimum thinking time per move
    int64_t move_overhead_ms = 20;  // Static overhead to subtract
    int64_t panic_margin_ms  = 80;  // Time reserved to avoid flagging
};

extern TimeModel GTime;

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
              double&             originalTimeAdjust,
              int                 evaluationCp);

    TimePoint optimum() const;
    TimePoint maximum() const;
    template<typename FUNC>
    TimePoint elapsed(FUNC nodes) const {
        return useNodesTime ? TimePoint(nodes()) : elapsed_time();
    }
    TimePoint elapsed_time() const { return now() - startTime; };

    void clear();
    void advance_nodes_time(std::int64_t nodes);

   private:
    TimePoint startTime;
    TimePoint optimumTime;
    TimePoint maximumTime;

    std::int64_t availableNodes = -1;     // When in 'nodes as time' mode
    bool         useNodesTime   = false;  // True if we are in 'nodes as time' mode
};

}  // namespace Stockfish

