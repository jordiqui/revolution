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

#ifndef BOOK_H_INCLUDED
#define BOOK_H_INCLUDED

#include <cstddef>
#include <string>

#include "../types.h"

namespace Stockfish {
class Position;

namespace Book {

struct LoadStats {
    int validMoves = 0;
    int totalMoves = 0;
};

class Book {
   public:
    virtual ~Book() = default;

    virtual std::string type() const                                         = 0;
    virtual bool        open(const std::string& filename)                     = 0;
    virtual void        close()                                                = 0;
    virtual Move        probe(const Position&, std::size_t width, bool onlyGreen) const = 0;
    virtual void        show_moves(const Position&) const                      = 0;
    virtual LoadStats   load_stats() const                                     = 0;
};

}  // namespace Book
}  // namespace Stockfish

#endif  // BOOK_H_INCLUDED
