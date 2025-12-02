#include "../uci.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <type_traits>

#include "book_manager.h"
#include "book_utils.h"
#include "ctg/ctg.h"
#include "polyglot/polyglot.h"

namespace Stockfish {

namespace {

template<typename T>
T get_option_or(const OptionsMap& options, const std::string& key, T defaultValue)
{
    if (!options.count(key))
        return defaultValue;

    if constexpr (std::is_same_v<T, std::string>)
        return std::string(options[key]);
    else
        return static_cast<T>(options[key]);
}

}  // namespace

BookManager::BookManager() {
    books.fill(nullptr);
    liveBookFallback = false;
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

    update_fallback_status(options);
}

void BookManager::init(int index, const OptionsMap& options) {
    assert(index < NumberOfBooks);

    delete books[index];
    books[index] = nullptr;

    const auto optionKey   = ::Stockfish::Book::format_option_key("CTG/BIN Book %d File", index + 1);
    const std::string file = get_option_or<std::string>(options, optionKey, "");

    if (Book::is_empty_filename(file))
    {
        update_fallback_status(options);
        return;
    }

    const std::string resolved = Book::map_path(file);
    std::unique_ptr<Book::Book> candidate(Book::Book::create_book(resolved));
    if (!candidate)
    {
        sync_cout << "info string Unknown book type: " << file << sync_endl;
        update_fallback_status(options);
        return;
    }

    if (!candidate->open(resolved))
    {
        update_fallback_status(options);
        return;
    }

    books[index] = candidate.release();
    liveBookFallback = false;
}

Move BookManager::probe(const Position& pos, const OptionsMap& options) const {
    const int moveNumber = 1 + pos.game_ply() / 2;

    for (int i = 0; i < NumberOfBooks; ++i)
    {
        if (!books[i])
            continue;

        const auto depthKey = ::Stockfish::Book::format_option_key("Book %d Depth", i + 1);
        if (get_option_or<int>(options, depthKey, 0) < moveNumber)
            continue;

        const auto widthKey = ::Stockfish::Book::format_option_key("Book %d Width", i + 1);
        const auto greenKey = ::Stockfish::Book::format_option_key("(CTG) Book %d Only Green", i + 1);

        const Move move = books[i]->probe(pos,
                                          size_t(get_option_or<int>(options, widthKey, 1)),
                                          static_cast<bool>(get_option_or<int>(options, greenKey, 0)));
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
            const auto stats = books[i]->load_stats();
            std::cout << "  Load stats: valid moves " << stats.validMoves
                      << ", ignored entries " << stats.ignoredEntries << std::endl;
            books[i]->show_moves(pos);
        }
    }

    if (liveBookFallback)
        std::cout << "Live book fallback active" << std::endl;
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

void BookManager::update_fallback_status(const OptionsMap& options) {
    bool anyRequested = false;
    for (int i = 0; i < NumberOfBooks; ++i)
    {
        const auto optionKey = ::Stockfish::Book::format_option_key("CTG/BIN Book %d File", i + 1);
        if (!Book::is_empty_filename(get_option_or<std::string>(options, optionKey, "")))
            anyRequested = true;
    }

    bool hasBook = false;
    for (auto* book : books)
    {
        if (book)
        {
            hasBook = true;
            break;
        }
    }

    if (!hasBook && anyRequested)
    {
        if (!liveBookFallback)
        {
            liveBookFallback = true;
            sync_cout << "info string All CTG/BIN books failed, falling back to live book" << sync_endl;
        }
    }
    else
        liveBookFallback = false;
}

void BookManager::set_book_for_testing(int index, Book::Book* book) {
    assert(index < NumberOfBooks);

    delete books[index];
    books[index] = book;

    bool hasBook = false;
    for (auto* b : books)
        hasBook |= (b != nullptr);

    liveBookFallback = !hasBook;
}

}  // namespace Stockfish

