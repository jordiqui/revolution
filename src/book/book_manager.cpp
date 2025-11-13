#include "../uci.h"

#include <cassert>
#include <iostream>
#include <memory>

#include "book_manager.h"
#include "book_utils.h"
#include "ctg/ctg.h"
#include "polyglot/polyglot.h"

namespace Stockfish {

BookManager::BookManager() {
    books.fill(nullptr);
}

BookManager::~BookManager() {
    for (auto*& book : books)
        delete book;
}

void BookManager::set_base_directory(const std::string& directory) {
    Book::set_base_directory(directory);
}

void BookManager::init(const OptionsMap& options) {
    for (int i = 0; i < NumberOfBooks; ++i)
        init(i, options);
}

void BookManager::init(int index, const OptionsMap& options) {
    assert(index < NumberOfBooks);

    delete books[index];
    books[index] = nullptr;

    const auto optionKey   = ::Stockfish::Book::format_option_key("CTG/BIN Book %d File", index + 1);
    const std::string file = std::string(options[optionKey]);

    if (Book::is_empty_filename(file))
        return;

    const std::string resolved = Book::map_path(file);
    std::unique_ptr<Book::Book> candidate(Book::Book::create_book(resolved));
    if (!candidate)
    {
        sync_cout << "info string Unknown book type: " << file << sync_endl;
        return;
    }

    if (!candidate->open(resolved))
        return;

    books[index] = candidate.release();
}

Move BookManager::probe(const Position& pos, const OptionsMap& options) const {
    const int moveNumber = 1 + pos.game_ply() / 2;

    for (int i = 0; i < NumberOfBooks; ++i)
    {
        if (!books[i])
            continue;

        const auto depthKey = ::Stockfish::Book::format_option_key("Book %d Depth", i + 1);
        if (int(options[depthKey]) < moveNumber)
            continue;

        const auto widthKey = ::Stockfish::Book::format_option_key("Book %d Width", i + 1);
        const auto greenKey = ::Stockfish::Book::format_option_key("(CTG) Book %d Only Green", i + 1);

        const Move move = books[i]->probe(pos, size_t(int(options[widthKey])),
                                          bool(options[greenKey]));
        if (move != Move::none())
            return move;
    }

    return Move::none();
}

void BookManager::show_moves(const Position& pos, const OptionsMap& options) const {
    std::cout << pos << std::endl << std::endl;

    for (int i = 0; i < NumberOfBooks; ++i)
    {
        const auto fileKey = ::Stockfish::Book::format_option_key("CTG/BIN Book %d File", i + 1);

        if (!books[i])
            std::cout << "Book " << i + 1 << ": No book loaded" << std::endl;
        else
        {
            std::cout << "Book " << i + 1 << " (" << books[i]->type() << "): "
                      << std::string(options[fileKey]) << std::endl;
            books[i]->show_moves(pos);
        }
    }
}

void BookManager::show_polyglot(const Position& pos, const OptionsMap& options) const {
    std::cout << pos << std::endl << std::endl;

    bool hasPolyglot = false;
    for (int i = 0; i < NumberOfBooks; ++i)
    {
        if (!books[i] || books[i]->type() != "BIN")
            continue;

        hasPolyglot = true;
        const auto fileKey = ::Stockfish::Book::format_option_key("CTG/BIN Book %d File", i + 1);
        std::cout << "Polyglot book " << i + 1 << ": " << std::string(options[fileKey]) << std::endl;
        books[i]->show_moves(pos);
    }

    if (!hasPolyglot)
        std::cout << "No Polyglot books loaded" << std::endl;
}

}  // namespace Stockfish

