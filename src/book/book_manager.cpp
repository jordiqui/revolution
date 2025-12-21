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

#include "book_manager.h"

#include <algorithm>
#include <iostream>

#include "book_utils.h"
#include "../position.h"
#include "../ucioption.h"

namespace Stockfish {

BookManager::BookManager() = default;

void BookManager::init(const OptionsMap& options) {
    fallback_active_ = false;

    for (std::size_t index = 0; index < books_.size(); ++index) {
        const auto filenameKey = Book::format_option_key("CTG/BIN Book %d File", int(index + 1));
        const std::string filename = options.count(filenameKey) ? std::string(options[filenameKey]) : "";

        if (filename.empty()) {
            if (books_[index])
                books_[index]->close();
            continue;
        }

        if (!books_[index]) {
            fallback_active_ = true;
            continue;
        }

        if (!books_[index]->open(filename))
            fallback_active_ = true;
    }
}

Move BookManager::probe(const Position& pos, const OptionsMap& options) const {
    for (std::size_t index = 0; index < books_.size(); ++index) {
        if (!books_[index])
            continue;

        const auto widthKey = Book::format_option_key("Book %d Width", int(index + 1));
        const auto onlyGreenKey = Book::format_option_key("(CTG) Book %d Only Green", int(index + 1));

        std::size_t width = 1;
        if (options.count(widthKey))
            width = std::max<std::size_t>(1, int(options[widthKey]));

        bool onlyGreen = false;
        if (options.count(onlyGreenKey))
            onlyGreen = bool(int(options[onlyGreenKey]));

        Move move = books_[index]->probe(pos, width, onlyGreen);
        if (move != Move::none())
            return move;
    }

    return Move::none();
}

void BookManager::show_moves(const Position& pos, const OptionsMap&) const {
    for (const auto& book : books_) {
        if (book)
            book->show_moves(pos);
    }

    if (fallback_active_)
        std::cout << "Live book fallback active" << std::endl;
}

void BookManager::set_book_for_testing(std::size_t index, Book::Book* book) {
    if (index >= books_.size())
        return;

    books_[index].reset(book);
}

}
