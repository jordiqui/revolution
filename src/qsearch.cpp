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

#include "search.h"

#include "movegen.h"

namespace Stockfish {

namespace Search {

bool should_extend_qsearch(const Position& pos, const Stack* ss, Depth depth);
bool is_forced_recapture(const Position& pos, const Stack* ss);

namespace {

// Returns true when the previous move resulted in the side to move
// having exactly one favourable recapture on the same target square.
bool has_unique_profitable_recapture(const Position& pos, const Stack* ss) {
    if (ss->ply == 0 || !(ss - 1)->currentMove.is_ok())
        return false;

    if (pos.captured_piece() == NO_PIECE)
        return false;

    const Square captureSquare = ((ss - 1)->currentMove).to_sq();

    int viableRecaptures = 0;
    for (const Move move : MoveList<CAPTURES>(pos))
    {
        if (move.to_sq() != captureSquare)
            continue;

        if (!pos.see_ge(move, VALUE_ZERO))
            continue;

        if (++viableRecaptures > 1)
            return false;
    }

    return viableRecaptures == 1;
}

}  // namespace

bool should_extend_qsearch(const Position& pos, const Stack* ss, Depth depth) {
    if (depth < 0)
        return false;

    if (ss->ply >= MAX_PLY - 1)
        return false;

    if (pos.checkers())
        return true;

    return has_unique_profitable_recapture(pos, ss);
}

bool is_forced_recapture(const Position& pos, const Stack* ss) {
    return has_unique_profitable_recapture(pos, ss);
}

}  // namespace Search

}  // namespace Stockfish
