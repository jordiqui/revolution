#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "bitboard.h"
#include "experience.h"
#include "position.h"
#include "uci.h"
#include "tt.h"

namespace Stockfish {
namespace Zobrist {
extern Key side;
}  // namespace Zobrist
std::string UCIEngine::square(Square s) {
    return std::string{char('a' + file_of(s)), char('1' + rank_of(s))};
}

std::string UCIEngine::move(Move m, bool chess960) {
    (void) chess960;
    return square(m.from_sq()) + square(m.to_sq());
}

TTEntry* TranspositionTable::first_entry(const Key) const {
    return nullptr;
}

void* std_aligned_alloc(size_t alignment, size_t size) {
    if (alignment < sizeof(void*))
        alignment = sizeof(void*);
    size = ((size + alignment - 1) / alignment) * alignment;
    return std::aligned_alloc(alignment, size);
}

void std_aligned_free(void* ptr) {
    std::free(ptr);
}

void* aligned_large_pages_alloc(size_t size) {
    return std_aligned_alloc(4096, size);
}

void aligned_large_pages_free(void* mem) {
    std_aligned_free(mem);
}

bool has_large_pages() {
    return false;
}

namespace Tablebases {

int MaxCardinality = 0;

WDLScore probe_wdl(Position&, ProbeState* result) {
    if (result)
        *result = FAIL;
    return WDLDraw;
}

int probe_dtz(Position&, ProbeState* result) {
    if (result)
        *result = FAIL;
    return 0;
}

void init(const std::string&, bool) {}
void release() {}

}  // namespace Tablebases
}  // namespace Stockfish

namespace {

struct BinBL {
    std::uint64_t key;
    std::int32_t  depth;
    std::int32_t  value;
    std::uint16_t move;
    std::uint16_t pad;
    std::int32_t  perf;
};

std::filesystem::path make_temp_path(const std::string& name) {
    auto dir = std::filesystem::temp_directory_path();
    return dir / std::filesystem::path(name);
}

}  // namespace

int main() {
    using namespace Stockfish;

    Bitboards::init();
    Position::init();

    Position pos;
    StateInfo st;
    pos.set("8/8/8/8/8/8/4K3/7k b - - 0 1", false, &st);
    const Key key = pos.key();

    if ((key & Zobrist::side) == 0)
    {
        std::cerr << "Test setup error: key does not have black-to-move flag" << std::endl;
        return 1;
    }

    const BinBL record{key, 12, 42, 0, 0, 0};

    const auto inputPath  = make_temp_path("brainlearn_black_flip_input.blk");
    const auto outputPath = make_temp_path("brainlearn_black_flip_output.blk");

    {
        std::ofstream out(inputPath, std::ios::binary);
        if (!out)
        {
            std::cerr << "Failed to create input file" << std::endl;
            return 1;
        }
        out.write(reinterpret_cast<const char*>(&record), sizeof(record));
    }

    Experience exp;
    exp.load(inputPath.string());
    exp.save(outputPath.string());

    BinBL stored{};
    {
        std::ifstream in(outputPath, std::ios::binary);
        if (!in)
        {
            std::cerr << "Failed to open output file" << std::endl;
            return 1;
        }
        in.read(reinterpret_cast<char*>(&stored), sizeof(stored));
        if (!in)
        {
            std::cerr << "Failed to read stored record" << std::endl;
            return 1;
        }
    }

    std::error_code ec;
    std::filesystem::remove(inputPath, ec);
    std::filesystem::remove(outputPath, ec);

    if (stored.value != -record.value)
    {
        std::cerr << "Stored value was not flipped for black-to-move record" << std::endl;
        return 1;
    }

    return 0;
}
