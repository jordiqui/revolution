#include "book/book.h"

#include <algorithm>
#include <cctype>

#include "book/ctg/ctg.h"
#include "book/polyglot/polyglot.h"

namespace Stockfish::Book {

Book* Book::create_book(const std::string& filename) {

    const auto extIndex = filename.find_last_of('.');
    if (extIndex == std::string::npos)
        return nullptr;

    auto ext = filename.substr(extIndex + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });

    if (ext == "ctg" || ext == "cto" || ext == "ctb")
        return new CTG::CtgBook();

    if (ext == "bin")
        return new Polyglot::PolyglotBook();

    return nullptr;
}

}  // namespace Stockfish::Book
