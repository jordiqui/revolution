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

struct LegacyEntry {
    std::uint64_t key;
    std::uint32_t move;
    std::int32_t  value;
    std::int32_t  depth;
    std::uint16_t count;
    std::uint8_t  pad[2];
};

#pragma pack(push,1)
struct HeaderV2 {
    std::uint8_t  version;
    std::uint64_t seed;
    std::uint32_t bucket_size;
    std::uint32_t entry_size;
};
struct MetaBlockV2 {
    std::uint32_t hash_bits;
    std::uint32_t reserved;
    std::uint16_t endian_tag;
    float         k_factor;
    std::uint64_t counters;
};
struct EntryV2 {
    std::uint64_t key;
    std::uint16_t move;
    std::int16_t  score;
    std::int16_t  depth;
    std::int16_t  count;
    std::int32_t  wins;
    std::int32_t  losses;
    std::int32_t  draws;
    std::int16_t  flags;
    std::int16_t  age;
    std::int16_t  pad;
};
#pragma pack(pop)
static_assert(sizeof(HeaderV2) == 1 + sizeof(std::uint64_t) + 2 * sizeof(std::uint32_t),
              "HeaderV2 must be 17 bytes");
static_assert(sizeof(MetaBlockV2) == sizeof(std::uint32_t) * 2 + sizeof(std::uint16_t)
                                         + sizeof(float) + sizeof(std::uint64_t),
              "MetaBlockV2 must be 22 bytes");
static_assert(sizeof(EntryV2) == 34, "EntryV2 must be 34 bytes");

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

    const LegacyEntry winningRecord{key,
                                    static_cast<std::uint32_t>(winningMove.raw()),
                                    -500,
                                    12,
                                    1,
                                    {0, 0}};
    const LegacyEntry losingRecord{key,
                                   static_cast<std::uint32_t>(losingMove.raw()),
                                   400,
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

    std::vector<EntryV2> stored(2);
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

        HeaderV2 header{};
        in.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!in || header.version != 2)
        {
            std::cerr << "Stored file has an invalid header" << std::endl;
            return 1;
        }

        std::error_code sizeEc;
        const auto       fileSize = std::filesystem::file_size(outputPath, sizeEc);
        if (sizeEc)
        {
            std::cerr << "Failed to determine output file size" << std::endl;
            return 1;
        }

        const std::size_t sigSize       = sizeof(kSignature) - 1;
        const std::size_t headerBasic   = sizeof(HeaderV2);
        const std::size_t metaBlockSize = sizeof(MetaBlockV2);

        if (static_cast<std::size_t>(fileSize) < sigSize + headerBasic)
        {
            std::cerr << "Stored file is shorter than expected" << std::endl;
            return 1;
        }

        const std::size_t headerRemaining = static_cast<std::size_t>(fileSize) - (sigSize + headerBasic);

        std::size_t metaBlocks = 0;
        for (std::size_t blocks = 1; blocks <= headerRemaining / metaBlockSize; ++blocks)
        {
            const std::size_t afterHeader = headerRemaining - blocks * metaBlockSize;
            if (afterHeader % header.entry_size == 0)
            {
                metaBlocks = blocks;
                break;
            }
        }

        if (!metaBlocks)
        {
            std::cerr << "Unable to determine metadata length" << std::endl;
            return 1;
        }

        for (std::size_t i = 0; i < metaBlocks; ++i)
        {
            MetaBlockV2 meta{};
            in.read(reinterpret_cast<char*>(&meta), sizeof(meta));
            if (!in)
            {
                std::cerr << "Stored file has a truncated metadata block" << std::endl;
                return 1;
            }
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

    std::map<std::uint16_t, int> storedScores;
    for (const auto& rec : stored)
        storedScores.emplace(rec.move, static_cast<int>(rec.score));

    const std::uint16_t winMoveId = static_cast<std::uint16_t>(winningRecord.move & 0xFFFF);
    if (storedScores[winMoveId] != -winningRecord.value)
    {
        std::cerr << "Winning move score was not flipped" << std::endl;
        return 1;
    }

    const std::uint16_t loseMoveId = static_cast<std::uint16_t>(losingRecord.move & 0xFFFF);
    if (storedScores[loseMoveId] != -losingRecord.value)
    {
        std::cerr << "Losing move score was not flipped" << std::endl;
        return 1;
    }

    return 0;
}
