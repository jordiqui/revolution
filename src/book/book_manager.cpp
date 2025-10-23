#include "book_manager.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <utility>

#include "book.h"
#include "ctg/ctg.h"
#include "polyglot/polyglot.h"
#include "../misc.h"
#include "../position.h"
#include "../ucioption.h"

namespace Stockfish {
namespace Book {

BookManager::BookManager() = default;
BookManager::~BookManager() = default;

void BookManager::set_binary_directory(std::string directory) {
    namespace fs = std::filesystem;

    if (directory.empty())
    {
        binaryDirectory.clear();
        return;
    }

    fs::path path(directory);
    binaryDirectory = path.lexically_normal().string();
}

void BookManager::init(const OptionsMap& options) {
    for (int i = 0; i < NumberOfBooks; ++i)
        init(i, options);
}

void BookManager::init(int index, const OptionsMap& options) {
    assert(index >= 0 && index < NumberOfBooks);

    books[index].reset();

    const std::string fileOption = "CTG/BIN Book " + std::to_string(index + 1) + " File";
    std::string       filename   = std::string(options[fileOption]);

    if (filename.empty())
        return;

    const std::string resolved = resolve_book_path(filename);

    std::unique_ptr<Book> book(Book::create_book(resolved));
    if (!book)
    {
        sync_cout << "info string Unknown book type: " << filename << sync_endl;
        return;
    }

    if (!book->open(resolved))
        return;

    books[index] = std::move(book);
}

Move BookManager::probe(const Position& pos, const OptionsMap& options) const {
    const int moveNumber = 1 + pos.game_ply() / 2;

    for (int i = 0; i < NumberOfBooks; ++i)
    {
        if (!books[i])
            continue;

        const std::string depthOption = "Book " + std::to_string(i + 1) + " Depth";
        if (int(options[depthOption]) < moveNumber)
            continue;

        const std::string widthOption = "Book " + std::to_string(i + 1) + " Width";
        const std::string greenOption = "(CTG) Book " + std::to_string(i + 1) + " Only Green";

        const size_t width     = static_cast<size_t>(int(options[widthOption]));
        const bool   onlyGreen = bool(options[greenOption]);

        if (Move bookMove = books[i]->probe(pos, width, onlyGreen); bookMove != Move::none())
            return bookMove;
    }

    return Move::none();
}

void BookManager::show_moves(const Position& pos, const OptionsMap& options) const {
    std::cout << pos << std::endl << std::endl;

    for (int i = 0; i < NumberOfBooks; ++i)
    {
        if (!books[i])
        {
            std::cout << "Book " << i + 1 << ": No book loaded" << std::endl;
            continue;
        }

        const std::string fileOption = "CTG/BIN Book " + std::to_string(i + 1) + " File";
        std::cout << "Book " << i + 1 << " (" << books[i]->type() << "): "
                  << std::string(options[fileOption]) << std::endl;
        books[i]->show_moves(pos);
    }
}

std::string BookManager::resolve_book_path(const std::string& filename) const {
    namespace fs = std::filesystem;

    fs::path path(filename);
    if (!path.is_absolute())
    {
        if (!binaryDirectory.empty())
            path = fs::path(binaryDirectory) / path;
        else
            path = fs::current_path() / path;
    }

    return path.lexically_normal().string();
}

}  // namespace Book
}  // namespace Stockfish
