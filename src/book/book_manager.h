#ifndef BOOKMANAGER_H_INCLUDED
#define BOOKMANAGER_H_INCLUDED

#include <array>
#include <string>

#include "../position.h"
#include "../ucioption.h"
#include "book.h"

namespace Stockfish {

class BookManager {
   public:
    static constexpr int NumberOfBooks = 2;

   private:
    std::array<Book::Book*, NumberOfBooks> books;

   public:
    BookManager();
    ~BookManager();

    BookManager(const BookManager&)            = delete;
    BookManager& operator=(const BookManager&) = delete;

    void set_base_directory(const std::string& directory);

    void init(const OptionsMap& options);
    void init(int index, const OptionsMap& options);
    Move probe(const Position& pos, const OptionsMap& options) const;
    void show_moves(const Position& pos, const OptionsMap& options) const;
    void show_polyglot(const Position& pos, const OptionsMap& options) const;
};

}  // namespace Stockfish

#endif
