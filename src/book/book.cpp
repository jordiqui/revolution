#include "../misc.h"
#include "../uci.h"
#include "polyglot/polyglot.h"
#include "ctg/ctg.h"
#include "book.h"

#include <algorithm>
#include <cctype>

namespace Stockfish {
namespace Book {

/*static*/ Book* Book::create_book(const std::string& filename) {
    const size_t extIndex = filename.find_last_of('.');
    if (extIndex == std::string::npos)
        return nullptr;

    std::string ext = filename.substr(extIndex + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (ext == "ctg" || ext == "cto" || ext == "ctb")
        return new CTG::CtgBook();
    if (ext == "bin")
        return new Polyglot::PolyglotBook();
    return nullptr;
}

}  // namespace Book
}  // namespace Stockfish
