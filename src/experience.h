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

  Modifications Copyright (C) 2024 Jorge Ruiz Centelles
*/

#pragma once

#include <cstdint>
#include <future>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "position.h"
#include "types.h"

namespace Stockfish {

struct ExperienceEntry {
    Move move;
    int  score;
    int  depth;
    int  count;
    std::uint64_t lastUse = 0;
};

class Experience {
   public:
    void clear();
    void load(const std::string& file);
    void load_async(const std::string& file);
    void wait_until_loaded() const;
    void save(const std::string& file) const;
    Move probe(Position& pos, int width, int evalImportance, int minDepth, int maxMoves);
    void update(Position& pos, Move move, int score, int depth);
    void show(const Position& pos, int evalImportance, int maxMoves) const;
    void set_limits(std::size_t maxPositions, std::size_t maxEntriesPerPosition);
    std::size_t max_positions_limit() const { return maxPositionsLimit; }
    std::size_t max_entries_per_position_limit() const { return maxEntriesPerPositionLimit; }

   private:
    bool                                                  is_ready() const;
    void                                                  enforce_limits();
    std::size_t                                           prune_entries_for_position(std::vector<ExperienceEntry>& vec);
    void                                                  prune_position_limit(bool reserveSlotForNewPosition = false);
    void                                                  log_prune_event(std::string_view reason,
                                                                           std::size_t     positionsRemoved,
                                                                           std::size_t     entriesRemoved);
    double                                                approximate_memory_usage_mib() const;
    std::unordered_map<Key, std::vector<ExperienceEntry>> table;
    bool                                                  binaryFormat     = false;
    bool                                                  brainLearnFormat = true;
    std::future<void>                                     loader;
    std::size_t                                           maxPositionsLimit            = 0;
    std::size_t                                           maxEntriesPerPositionLimit   = 0;
    std::size_t                                           totalEntries                 = 0;
    std::size_t                                           totalPrunedPositions         = 0;
    std::size_t                                           totalPrunedEntries           = 0;
    std::size_t                                           totalPerPositionEvictions    = 0;
    std::uint64_t                                         updateCounter                = 0;
};

extern Experience experience;

}  // namespace Stockfish

