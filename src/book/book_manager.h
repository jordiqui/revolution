#ifndef BOOKMANAGER_H_INCLUDED
#define BOOKMANAGER_H_INCLUDED

#include <array>
#include <memory>
#include <string>

#include "book/book.h"
#include "position.h"
#include "ucioption.h"

namespace Stockfish {

class BookManager {
   public:
    static constexpr int NumberOfBooks = 2;

    BookManager();
    ~BookManager();

    BookManager(const BookManager&)            = delete;
    BookManager& operator=(const BookManager&) = delete;

    void set_base_directory(std::string baseDir);

    void init(const OptionsMap& options);
    void init(int index, const OptionsMap& options);
    Move probe(const Position& pos, const OptionsMap& options) const;
    void show_moves(const Position& pos, const OptionsMap& options) const;

   private:
    struct BookSlot {
        std::unique_ptr<Book::Book> book;
        std::string                 resolvedPath;
    };

    std::array<BookSlot, NumberOfBooks> books;
    std::string                         baseDirectory;

    static std::string format_option_name(const char* fmt, int index);
    std::string        resolve_path(std::string filename) const;
};

}  // namespace Stockfish

#endif  // #ifndef BOOKMANAGER_H_INCLUDED
