#include "book/book_manager.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <utility>

#include "book/book.h"
#include "misc.h"

namespace Stockfish {

namespace {

constexpr std::string_view EmptyToken = "<empty>";

std::string unquote(std::string value) {
    if (value.size() >= 2
        && ((value.front() == '\"' && value.back() == '\"')
            || (value.front() == '\'' && value.back() == '\'')))
        value = value.substr(1, value.size() - 2);

    return value;
}

bool equals_ignore_case(std::string_view lhs, std::string_view rhs) {
    return lhs.size() == rhs.size()
           && std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char a, char b) {
                  return std::tolower(static_cast<unsigned char>(a))
                      == std::tolower(static_cast<unsigned char>(b));
              });
}

}  // namespace

BookManager::BookManager() = default;
BookManager::~BookManager() = default;

void BookManager::set_base_directory(std::string baseDir) { baseDirectory = std::move(baseDir); }

void BookManager::init(const OptionsMap& options) {
    for (int i = 0; i < NumberOfBooks; ++i)
        init(i, options);
}

void BookManager::init(int index, const OptionsMap& options) {
    assert(index < NumberOfBooks);

    books[index].book.reset();
    books[index].resolvedPath.clear();

    const auto optionName = format_option_name("CTG/BIN Book %d File", index + 1);
    auto       filename   = std::string(options[optionName]);

    filename = unquote(filename);
    if (filename.empty() || equals_ignore_case(filename, EmptyToken))
        return;

    auto resolved = resolve_path(filename);
    auto bookPtr  = std::unique_ptr<Book::Book>(Book::Book::create_book(resolved));

    if (!bookPtr)
    {
        sync_cout << "info string Unknown book type: " << filename << sync_endl;
        return;
    }

    if (!bookPtr->open(resolved))
        return;

    books[index].resolvedPath = std::move(resolved);
    books[index].book         = std::move(bookPtr);
}

Move BookManager::probe(const Position& pos, const OptionsMap& options) const {
    const int moveNumber = 1 + pos.game_ply() / 2;

    for (int i = 0; i < NumberOfBooks; ++i)
    {
        const auto& slot = books[i];

        if (!slot.book)
            continue;

        const auto depthOption = format_option_name("Book %d Depth", i + 1);
        if (int(options[depthOption]) < moveNumber)
            continue;

        const auto widthOption = format_option_name("Book %d Width", i + 1);
        const auto greenOption = format_option_name("(CTG) Book %d Only Green", i + 1);

        const auto width = std::max(1, int(options[widthOption]));
        const auto move  = slot.book->probe(pos, size_t(width), options[greenOption]);

        if (move != Move::none())
            return move;
    }

    return Move::none();
}

void BookManager::show_moves(const Position& pos, const OptionsMap& options) const {
    std::cout << pos << "\n\n";

    for (int i = 0; i < NumberOfBooks; ++i)
    {
        const auto& slot       = books[i];
        const auto  fileOption = format_option_name("CTG/BIN Book %d File", i + 1);
        const auto  fileName   = std::string(options[fileOption]);

        if (!slot.book)
        {
            std::cout << "Book " << i + 1 << ": No book loaded" << std::endl;
            continue;
        }

        std::cout << "Book " << i + 1 << " (" << slot.book->type() << "): " << fileName << std::endl;
        slot.book->show_moves(pos);
    }
}

std::string BookManager::format_option_name(const char* fmt, int index) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), fmt, index);
    return buffer;
}

std::string BookManager::resolve_path(std::string filename) const {
    namespace fs = std::filesystem;

    fs::path p = fs::u8path(filename);

    if (!p.is_absolute())
    {
        if (!baseDirectory.empty())
            p = fs::u8path(baseDirectory) / p;
        else
            p = fs::current_path() / p;
    }

    std::error_code ec;
    auto            normal = fs::weakly_canonical(p, ec);
    if (!ec)
        return normal.u8string();

    return fs::absolute(p).lexically_normal().u8string();
}

}  // namespace Stockfish
