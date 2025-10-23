#ifndef BOOKMANAGER_H_INCLUDED
#define BOOKMANAGER_H_INCLUDED

﻿#ifndef BOOKMANAGER_H_INCLUDED
#define BOOKMANAGER_H_INCLUDED

#include <array>
#include <memory>
#include <string>

#include "book.h"

namespace Stockfish {

class OptionsMap;
class Position;

namespace Book {

class BookManager {
   public:
    static constexpr int NumberOfBooks = 2;

    BookManager();
    ~BookManager();

    BookManager(const BookManager&)            = delete;
    BookManager& operator=(const BookManager&) = delete;

    void set_binary_directory(std::string directory);

    void init(const OptionsMap& options);
    void init(int index, const OptionsMap& options);
    Move probe(const Position& pos, const OptionsMap& options) const;
    void show_moves(const Position& pos, const OptionsMap& options) const;

   private:
    std::array<std::unique_ptr<Book>, NumberOfBooks> books;
    std::string                                      binaryDirectory;

    std::string resolve_book_path(const std::string& filename) const;
};

}  // namespace Book
}  // namespace Stockfish

#endif  // BOOKMANAGER_H_INCLUDED
