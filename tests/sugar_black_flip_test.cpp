#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "bitboard.h"
#include "experience.h"
#include "position.h"
#include "uci.h"
#include "tt.h"

namespace Stockfish {
namespace Zobrist {
extern Key side;
}  // namespace Zobrist
}  // namespace Stockfish

namespace {

struct BinV2 {
    std::uint64_t key;
    std::uint32_t move;
    std::int32_t  value;
    std::int32_t  depth;
    std::uint16_t count;
    std::uint8_t  pad[2];
};

constexpr char kSignature[] = "SugaR Experience version 2";

std::filesystem::path make_temp_path(const std::string& name) {
    auto dir = std::filesystem::temp_directory_path();
    return dir / std::filesystem::path(name);
}

}  // namespace

namespace Stockfish {

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

    const Move winningMove(SQ_H1, SQ_G1);
    const Move losingMove(SQ_H1, SQ_H2);

    // SugaR experience files already store evaluations from the side-to-move
    // perspective, matching what experience.save() produces.
    const BinV2 winningRecord{key,
                              static_cast<std::uint32_t>(winningMove.raw()),
                              500,
                              12,
                              1,
                              {0, 0}};
    const BinV2 losingRecord{key,
                             static_cast<std::uint32_t>(losingMove.raw()),
                             -400,
                             12,
                             1,
                             {0, 0}};

    const auto inputPath  = make_temp_path("sugar_black_flip_input.exp");
    const auto outputPath = make_temp_path("sugar_black_flip_output.exp");

    {
        std::ofstream out(inputPath, std::ios::binary);
        if (!out)
        {
            std::cerr << "Failed to create input file" << std::endl;
            return 1;
        }
        out.write(kSignature, sizeof(kSignature) - 1);
        out.write(reinterpret_cast<const char*>(&winningRecord), sizeof(winningRecord));
        out.write(reinterpret_cast<const char*>(&losingRecord), sizeof(losingRecord));
    }

    Experience exp;
    exp.load(inputPath.string());

    Move best = exp.probe(pos, /*width*/ 8, /*evalImportance*/ 0, /*minDepth*/ 0, /*maxMoves*/ 8);
    if (best != winningMove)
    {
        std::cerr << "Experience probe did not pick the winning move" << std::endl;
        return 1;
    }

    exp.save(outputPath.string());

    std::vector<BinV2> stored(2);
    {
        std::ifstream in(outputPath, std::ios::binary);
        if (!in)
        {
            std::cerr << "Failed to open output file" << std::endl;
            return 1;
        }

        std::vector<char> signature(sizeof(kSignature) - 1);
        in.read(signature.data(), signature.size());
        if (!in || std::string(signature.begin(), signature.end()) != kSignature)
        {
            std::cerr << "Stored file is missing SugaR signature" << std::endl;
            return 1;
        }

        for (auto& rec : stored)
        {
            in.read(reinterpret_cast<char*>(&rec), sizeof(rec));
            if (!in)
            {
                std::cerr << "Failed to read stored record" << std::endl;
                return 1;
            }
        }
    }

    std::error_code ec;
    std::filesystem::remove(inputPath, ec);
    std::filesystem::remove(outputPath, ec);

    std::map<std::uint32_t, std::int32_t> storedScores;
    for (const auto& rec : stored)
        storedScores.emplace(rec.move, rec.value);

    if (storedScores[winningRecord.move] != winningRecord.value)
    {
        std::cerr << "Winning move score changed unexpectedly" << std::endl;
        return 1;
    }

    if (storedScores[losingRecord.move] != losingRecord.value)
    {
        std::cerr << "Losing move score changed unexpectedly" << std::endl;
        return 1;
    }

    return 0;
}
