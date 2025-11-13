#include "../misc.h"
#include "../uci.h"
#include "polyglot/polyglot.h"
#include "ctg/ctg.h"
#include "book.h"

#include <cctype>
#include <filesystem>

namespace Stockfish {
namespace Book {
/*static*/ Book* Book::create_book(const std::string& filename) {
    const auto extension = std::filesystem::path(filename).extension().string();

    if (extension.empty())
        return nullptr;

    std::string normalized = extension;
    if (!normalized.empty() && normalized.front() == '.')
        normalized.erase(0, 1);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (normalized == "ctg" || normalized == "cto" || normalized == "ctb")
        return new CTG::CtgBook();
    else if (normalized == "bin")
        return new Polyglot::PolyglotBook();
    else
        return nullptr;
}
}
}
