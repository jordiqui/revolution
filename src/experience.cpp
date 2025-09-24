#include "experience.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <future>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string_view>
#include <limits>
#include <vector>
#include <zlib.h>

#include "misc.h"
#include "position.h"
#include "uci.h"

#if defined(_MSC_VER)
#    define PACKED_STRUCT_BEGIN __pragma(pack(push, 1))
#    define PACKED_STRUCT_END __pragma(pack(pop))
#    define PACKED_STRUCT struct
#else
#    define PACKED_STRUCT_BEGIN
#    define PACKED_STRUCT_END
#    define PACKED_STRUCT struct __attribute__((packed))
#endif

namespace {

PACKED_STRUCT_BEGIN
PACKED_STRUCT BrainLearnRecord {
    std::uint64_t key;
    std::int32_t  depth;
    std::int32_t  value;
    std::uint16_t move;
    std::uint16_t pad;
    std::int32_t  perf;
};
PACKED_STRUCT_END

PACKED_STRUCT_BEGIN
PACKED_STRUCT SugaRBinV1 {
    std::uint64_t key;
    std::uint32_t move;
    std::int32_t  value;
    std::int32_t  depth;
    std::uint8_t  pad[4];
};

PACKED_STRUCT SugaRBinV2Minimal {
    std::uint64_t key;
    std::uint32_t move;
    std::int32_t  value;
    std::int32_t  depth;
    std::uint16_t count;
    std::uint8_t  pad[2];
};

PACKED_STRUCT SugaRBinV2Full {
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
PACKED_STRUCT_END

PACKED_STRUCT_BEGIN
PACKED_STRUCT SugaRHeader {
    std::uint8_t  version;
    std::uint64_t seed;
    std::uint32_t bucketSize;
    std::uint32_t entrySize;
};

PACKED_STRUCT SugaRMetaBlock {
    std::uint32_t hashBits;
    std::uint32_t reserved;
    std::uint16_t endianTag;
    float         kFactor;
    std::uint64_t counters;
};

PACKED_STRUCT SugaRBinV2 {
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
PACKED_STRUCT_END

constexpr std::size_t BrainLearnHeaderCandidate = 8;

bool has_brainlearn_header_prefix(std::string_view buffer) {
    if (buffer.size() < 2)
        return false;
    return (buffer[0] == 'B' || buffer[0] == 'b') && (buffer[1] == 'L' || buffer[1] == 'l');
}

}  // namespace

namespace Stockfish {

namespace Zobrist {
extern Key side;
}

Experience experience;

void Experience::wait_until_loaded() const {
    if (loader.valid())
        loader.wait();
}

bool Experience::is_ready() const {
    return !loader.valid() || loader.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

void Experience::clear() {
    wait_until_loaded();
    table.clear();
    brainLearnHeaderData.clear();
}

void Experience::load(const std::string& file) {
    std::string path       = file;
    bool        convertBin = false;
    bool        compressed = false;
    std::string display;

    if (path.size() >= 4)
    {
        std::string ext = path.substr(path.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });

        if (ext == ".bin")
        {
            convertBin = true;
            path       = path.substr(0, path.size() - 4) + ".exp";
            sync_cout << "info string '.bin' experience files are deprecated; converting to '"
                      << path << "'" << sync_endl;
        }
        else if (ext == ".ccz")
            compressed = true;
        else if (ext == ".exp")
        {
            // Default uncompressed experience format
        }
    }

    display = path;
    if (path != file)
        display += " (from " + file + ")";

    brainLearnHeaderData.clear();

    std::string buffer;
    if (compressed)
    {
        gzFile gzin = gzopen(path.c_str(), "rb");
        if (!gzin)
        {
            sync_cout << "info string Could not open " << display << sync_endl;
            return;
        }
        char tmp[1 << 15];
        int  bytes;
        while ((bytes = gzread(gzin, tmp, sizeof(tmp))) > 0)
            buffer.append(tmp, bytes);
        gzclose(gzin);
    }
    else
    {
        std::ifstream f(convertBin ? file : path, std::ios::binary);
        if (!f)
        {
            sync_cout << "info string Could not open " << display << sync_endl;
            return;
        }
        buffer.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }

    std::istringstream in(buffer);

    const std::string sigV2 = "SugaR Experience version 2";
    const std::string sigV1 = "SugaR";
    std::string       header(sigV2.size(), '\0');
    in.read(header.data(), header.size());
    bool        isV2 = header == sigV2;
    bool        isV1 = !isV2 && header.substr(0, sigV1.size()) == sigV1;
    bool        isBL = false;
    std::size_t blOffset = 0;

    if (!isV1 && !isV2 && buffer.size() >= sizeof(BrainLearnRecord))
    {
        constexpr std::array<std::size_t, 3> offsets = {0u, BrainLearnHeaderCandidate,
                                                        2 * BrainLearnHeaderCandidate};
        const std::size_t                     size    = buffer.size();

        for (const auto offset : offsets)
        {
            if (size <= offset)
                continue;

            if ((size - offset) % sizeof(BrainLearnRecord) != 0)
                continue;

            if (offset == 0
                || has_brainlearn_header_prefix(std::string_view(buffer).substr(0, offset)))
            {
                isBL     = true;
                blOffset = offset;
                if (offset > 0)
                    brainLearnHeaderData.assign(buffer.data(), offset);
                break;
            }
        }
    }
    in.clear();
    in.seekg(0, std::ios::beg);

    table.clear();
    binaryFormat     = isV1 || isV2 || isBL;
    brainLearnFormat = isBL;

    std::size_t totalMoves     = 0;
    std::size_t duplicateMoves = 0;

    auto insert_entry = [&](uint64_t key, unsigned move, int score, int depth, int count) {
        totalMoves++;
        auto& vec = table[key];
        bool  dup = false;
        for (auto& e : vec)
            if (e.move.raw() == move)
            {
                dup = true;
                duplicateMoves++;
                e.score = score;
                e.depth = depth;
                e.count += count;
                break;
            }
        if (!dup)
            vec.push_back({Move(static_cast<std::uint16_t>(move)), score, depth, count});
    };

    if (binaryFormat)
    {
        if (isBL)
        {
            in.seekg(static_cast<std::streamoff>(blOffset), std::ios::beg);

            BrainLearnRecord e{};
            while (in.read(reinterpret_cast<char*>(&e), sizeof(e)))
            {
                int value = e.value;
                if (e.key & Zobrist::side)
                    value = -value;
                const int storedCount = std::max(1, e.perf);
                insert_entry(e.key, e.move, value, e.depth, storedCount);
            }
        }
        else
        {
            in.seekg(isV2 ? sigV2.size() : sigV1.size(), std::ios::beg);

            if (isV2)
            {
                struct V2HeaderInfo {
                    std::uint8_t version;
                    std::uint64_t seed;
                    std::uint32_t bucketSize;
                    std::uint32_t entrySize;
                    std::size_t   metaBlocks;
                    std::size_t   headerBytes;
                };

                auto parse_v2_header = [&](V2HeaderInfo& out) {
                    const std::size_t signatureSize = sigV2.size();
                    const std::size_t minHeader     = 1 + sizeof(std::uint64_t) + sizeof(std::uint32_t) * 2;

                    if (buffer.size() < signatureSize + minHeader)
                        return false;

                    const auto* data = reinterpret_cast<const std::uint8_t*>(buffer.data());
                    const auto* end  = data + buffer.size();
                    const auto* ptr  = data + signatureSize;

                    auto read_u32 = [&](const std::uint8_t*& p) {
                        std::uint32_t v = std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8)
                                          | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
                        p += 4;
                        return v;
                    };
                    auto read_u64 = [&](const std::uint8_t*& p) {
                        std::uint64_t v = 0;
                        for (int i = 0; i < 8; ++i)
                            v |= std::uint64_t(p[i]) << (8 * i);
                        p += 8;
                        return v;
                    };

                    out.version    = *ptr++;
                    out.seed       = read_u64(ptr);
                    out.bucketSize = read_u32(ptr);
                    out.entrySize  = read_u32(ptr);

                    if (out.version != 2 || out.entrySize < sizeof(SugaRBinV2Full)
                        || out.entrySize > 4096)
                        return false;

                    const std::size_t metaSize  = sizeof(std::uint32_t) * 2 + sizeof(std::uint16_t)
                                                  + sizeof(float) + sizeof(std::uint64_t);
                    const std::size_t available = static_cast<std::size_t>(end - ptr);

                    if (available < metaSize)
                        return false;

                    const std::size_t maxBlocks = available / metaSize;
                    std::size_t       chosen    = 0;

                    for (std::size_t blocks = 1; blocks <= maxBlocks; ++blocks)
                    {
                        const std::size_t afterHeader = available - blocks * metaSize;
                        if (out.entrySize && afterHeader % out.entrySize == 0)
                        {
                            chosen = blocks;
                            break;
                        }
                    }

                    if (!chosen)
                        return false;

                    out.metaBlocks = chosen;
                    out.headerBytes = minHeader + chosen * metaSize;

                    for (std::size_t i = 0; i < chosen; ++i)
                    {
                        const std::uint32_t hashBits = read_u32(ptr);
                        const std::uint32_t reserved = read_u32(ptr);
                        const std::uint16_t endian   = std::uint16_t(ptr[0]) | (std::uint16_t(ptr[1]) << 8);
                        ptr += sizeof(std::uint16_t);
                        ptr += sizeof(float);  // k factor
                        ptr += sizeof(std::uint64_t);  // counters

                        (void) hashBits;
                        (void) reserved;

                        if (endian != 0x0002)
                            return false;
                    }

                    return true;
                };

                V2HeaderInfo headerInfo{};
                bool         hasHeader = parse_v2_header(headerInfo);

                std::size_t entryOffset = sigV2.size();
                std::size_t entrySize   = sizeof(SugaRBinV2Minimal);

                if (hasHeader)
                {
                    entryOffset += headerInfo.headerBytes;
                    entrySize = headerInfo.entrySize;
                }

                in.seekg(static_cast<std::streamoff>(entryOffset), std::ios::beg);

                if (hasHeader)
                {
                    std::vector<char> entry(entrySize);
                    while (in.read(entry.data(), static_cast<std::streamsize>(entrySize)))
                    {
                        const auto* ptr = reinterpret_cast<const std::uint8_t*>(entry.data());
                        auto        read_u64_entry = [&](const std::uint8_t*& p) {
                            std::uint64_t v = 0;
                            for (int i = 0; i < 8; ++i)
                                v |= std::uint64_t(p[i]) << (8 * i);
                            p += 8;
                            return v;
                        };

                        auto read_u16 = [&](const std::uint8_t*& p) {
                            std::uint16_t v = std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8);
                            p += 2;
                            return v;
                        };
                        auto read_i16 = [&](const std::uint8_t*& p) {
                            std::uint16_t raw = std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8);
                            p += 2;
                            return static_cast<std::int16_t>(raw);
                        };

                        const std::uint64_t key    = read_u64_entry(ptr);
                        const std::uint16_t move16 = read_u16(ptr);
                        const std::int16_t score = read_i16(ptr);
                        const std::int16_t depth = read_i16(ptr);
                        const std::int16_t count = read_i16(ptr);

                        // Skip wins/losses/draws/flags/age/padding
                        ptr += sizeof(std::int32_t) * 3;  // wins, losses, draws
                        ptr += sizeof(std::int16_t) * 3;  // flags, age, pad

                        int value = score;
                        if (key & Zobrist::side)
                            value = -value;

                        const int storedDepth = depth;
                        const int storedCount = std::max<int>(1, count);

                        insert_entry(key, static_cast<unsigned>(move16), value, storedDepth, storedCount);
                    }
                }
                else
                {
                    SugaRBinV2Minimal e{};
                    while (in.read(reinterpret_cast<char*>(&e), sizeof(e)))
                    {
                        int value = e.value;
                        if (e.key & Zobrist::side)
                            value = -value;
                        insert_entry(e.key, e.move, value, e.depth, e.count);
                    }
                }
            }
            else
            {
                SugaRBinV1 e{};
                while (in.read(reinterpret_cast<char*>(&e), sizeof(e)))
                {
                    int value = e.value;
                    if (e.key & Zobrist::side)
                        value = -value;
                    insert_entry(e.key, e.move, value, e.depth, 1);
                }
            }
        }
    }
    else
    {
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
                continue;

            std::istringstream iss(line);
            std::string keyStr, moveStr;
            int         score, depth;
            int         count = 1;

            if (!(iss >> keyStr >> moveStr >> score >> depth))
                continue;

            if (!(iss >> count))
            {
                // Older text files omit the occurrence count. Treat them as
                // having a single observation so we remain backward
                // compatible instead of discarding the entire entry.
                count = 1;
            }

            auto parse = [](const std::string& s, uint64_t& out) {
                std::istringstream ss(s);
                if (s.find_first_not_of("0123456789") == std::string::npos)
                    ss >> out;
                else
                    ss >> std::hex >> out;
                return !ss.fail();
            };

            uint64_t key64, move64;
            if (!parse(keyStr, key64) || !parse(moveStr, move64))
                continue;
            insert_entry(key64, static_cast<unsigned>(move64), score, depth, count);
        }
    }

    std::size_t totalPositions = table.size();
    double      frag           = totalPositions ? 100.0 * duplicateMoves / totalPositions : 0.0;

    sync_cout << "info string " << display << " -> Total moves: " << totalMoves
              << ". Total positions: " << totalPositions << ". Duplicate moves: " << duplicateMoves
              << ". Fragmentation: " << std::fixed << std::setprecision(2) << frag << "%)"
              << sync_endl;

    binaryFormat = true;

    if (convertBin)
        save(path);
}

void Experience::load_async(const std::string& file) {
    loader = std::async(std::launch::async, [this, file] { load(file); });
}

void Experience::save(const std::string& file) const {
    wait_until_loaded();
    std::string path       = file;
    bool        compressed = false;

    if (path.size() >= 4)
    {
        std::string ext = path.substr(path.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });

        if (ext == ".bin")
        {
            path = path.substr(0, path.size() - 4) + ".exp";
            sync_cout << "info string '.bin' experience files are deprecated; saving to '" << path
                      << "'" << sync_endl;
        }
        else if (ext == ".ccz")
            compressed = true;
        else if (ext == ".exp")
        {
            // Default uncompressed experience format
        }
    }

    std::string buffer;
    std::size_t totalMoves = 0;

    if (brainLearnFormat)
    {
        if (!brainLearnHeaderData.empty())
            buffer.append(brainLearnHeaderData);

        for (const auto& [key, vec] : table)
            for (const auto& e : vec)
            {
                BrainLearnRecord be{key,
                                    e.depth,
                                    e.score,
                                    static_cast<std::uint16_t>(e.move.raw()),
                                    0,
                                    std::clamp(e.count,
                                               1,
                                               std::numeric_limits<std::int32_t>::max())};
                buffer.append(reinterpret_cast<const char*>(&be), sizeof(be));
                totalMoves++;
            }
    }
    else
    {
        const std::string sig = "SugaR Experience version 2";
        buffer.append(sig);

        static_assert(sizeof(SugaRHeader) == 1 + sizeof(std::uint64_t) + 2 * sizeof(std::uint32_t),
                      "Unexpected header packing");
        static_assert(sizeof(SugaRMetaBlock) == sizeof(std::uint32_t) * 2 + sizeof(std::uint16_t)
                                                + sizeof(float) + sizeof(std::uint64_t),
                      "Unexpected meta block packing");
        static_assert(sizeof(SugaRBinV2) == 34, "Unexpected V2 entry size");

        const SugaRHeader header{2, 0x103380A463E28000ULL, 6u,
                                 static_cast<std::uint32_t>(sizeof(SugaRBinV2))};
        const SugaRMetaBlock metaBlocks[2] = {{23u, 1u, 0x0002u, 11.978f, 0u},
                                              {23u, 1u, 0x0002u, 11.978f, 0u}};

        buffer.append(reinterpret_cast<const char*>(&header), sizeof(header));
        buffer.append(reinterpret_cast<const char*>(metaBlocks), sizeof(metaBlocks));

        auto clamp_i16 = [](int value) {
            if (value > std::numeric_limits<std::int16_t>::max())
                return std::numeric_limits<std::int16_t>::max();
            if (value < std::numeric_limits<std::int16_t>::min())
                return std::numeric_limits<std::int16_t>::min();
            return static_cast<std::int16_t>(value);
        };
        auto clamp_count = [](int value) {
            if (value <= 0)
                return std::int16_t(1);
            if (value > std::numeric_limits<std::int16_t>::max())
                return std::numeric_limits<std::int16_t>::max();
            return static_cast<std::int16_t>(value);
        };

        for (const auto& [key, vec] : table)
            for (const auto& e : vec)
            {
                SugaRBinV2 be{};
                be.key   = key;
                be.move  = static_cast<std::uint16_t>(e.move.raw());
                be.score = clamp_i16(e.score);
                be.depth = clamp_i16(e.depth);
                be.count = clamp_count(e.count);
                be.wins  = 0;
                be.losses = 0;
                be.draws  = 0;
                be.flags  = 0;
                be.age    = 0;
                be.pad    = 0;

                buffer.append(reinterpret_cast<const char*>(&be), sizeof(be));
                totalMoves++;
            }
    }

    bool ok = false;
    if (compressed)
    {
        gzFile out = gzopen(path.c_str(), "wb9");
        if (out)
        {
            if (gzwrite(out, buffer.data(), buffer.size()) == (int) buffer.size())
                ok = true;
            gzclose(out);
        }
    }
    else
    {
        std::ofstream out(path, std::ios::binary);
        if (out)
        {
            out.write(buffer.data(), buffer.size());
            ok = bool(out);
        }
    }

    if (!ok)
    {
        sync_cout << "info string Could not open " << path << " for writing" << sync_endl;
        return;
    }

    std::size_t totalPositions = table.size();

    sync_cout << "info string " << path << " <- Total moves: " << totalMoves
              << ". Total positions: " << totalPositions << sync_endl;
}

namespace {

int oriented_score(const ExperienceEntry& entry, Color sideToMove)
{
    return sideToMove == Color::WHITE ? entry.score : -entry.score;
}

}  // namespace

Move Experience::probe(Position& pos, int width, int evalImportance, int minDepth, int maxMoves) {
    if (!is_ready())
        return Move::none();
    auto it = table.find(pos.key());
    if (it == table.end())
        return Move::none();

    auto vec = it->second;
    if (vec.empty())
        return Move::none();

    const Color sideToMove = pos.side_to_move();

    // Order moves by their historical evaluation and depth so that the most
    // promising moves come first.  This allows the engine to "learn" from
    // previous games by preferring moves with the best average score at the
    // deepest search.
    std::sort(vec.begin(), vec.end(), [&](const ExperienceEntry& a, const ExperienceEntry& b) {
        const int aScore = oriented_score(a, sideToMove) + evalImportance * a.depth;
        const int bScore = oriented_score(b, sideToMove) + evalImportance * b.depth;
        return aScore > bScore;
    });

    vec.resize(std::min<int>({maxMoves, width, static_cast<int>(vec.size())}));

    if (vec.empty() || vec.front().depth < minDepth)
        return Move::none();

    // Pick the best move deterministically instead of randomly.  The highest
    // ranked move represents the one with the best historical evaluation.
    return vec.front().move;
}

void Experience::update(Position& pos, Move move, int score, int depth) {
    wait_until_loaded();
    if (!is_ready())
        return;
    const int storedScore = pos.side_to_move() == Color::WHITE ? score : -score;

    auto& vec = table[pos.key()];
    for (auto& e : vec)
        if (e.move == move)
        {
            // Update the stored statistics with a running average of the
            // evaluation.  This simple learning mechanism increases the score
            // reliability over time and retains the deepest search depth seen.
            e.score = (e.score * e.count + storedScore) / (e.count + 1);
            e.depth = std::max(e.depth, depth);
            e.count++;
            return;
        }
    // First encounter of this move in the current position.
    vec.push_back({move, storedScore, depth, 1});
}

void Experience::show(const Position& pos, int evalImportance, int maxMoves) const {
    wait_until_loaded();
    if (!is_ready())
        return;
    auto it = table.find(pos.key());
    if (it == table.end())
    {
        sync_cout << "info string No experience available" << sync_endl;
        return;
    }
    auto  vec        = it->second;
    Color sideToMove = pos.side_to_move();
    std::sort(vec.begin(), vec.end(), [&](const ExperienceEntry& a, const ExperienceEntry& b) {
        const int aScore = oriented_score(a, sideToMove) + evalImportance * a.depth;
        const int bScore = oriented_score(b, sideToMove) + evalImportance * b.depth;
        return aScore > bScore;
    });
    int shown = 0;
    for (const auto& e : vec)
    {
        if (shown++ >= maxMoves)
            break;
        const int oriented = oriented_score(e, sideToMove);
        sync_cout << "info string " << UCIEngine::move(e.move, pos.is_chess960()) << " score "
                  << oriented << " depth " << e.depth << " count " << e.count << sync_endl;
    }
}

}  // namespace Stockfish

#undef PACKED_STRUCT_BEGIN
#undef PACKED_STRUCT_END
#undef PACKED_STRUCT
