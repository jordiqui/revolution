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

#ifndef BOOK_MANAGER_H_INCLUDED
#define BOOK_MANAGER_H_INCLUDED

#include <array>
#include <cstddef>
#include <memory>

#include "book.h"

namespace Stockfish {
class OptionsMap;
class Position;

class BookManager {
   public:
    BookManager();

    void init(const OptionsMap& options);
    Move probe(const Position& pos, const OptionsMap& options) const;
    void show_moves(const Position& pos, const OptionsMap& options) const;

    void set_book_for_testing(std::size_t index, Book::Book* book);

   private:
    static constexpr std::size_t BookSlots = 2;

    std::array<std::unique_ptr<Book::Book>, BookSlots> books_{};
    bool fallback_active_ = false;
};

}  // namespace Stockfish

#endif  // BOOK_MANAGER_H_INCLUDED
