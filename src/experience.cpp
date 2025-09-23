#include "experience.h"

#include <algorithm>
#include <cctype>
#include <future>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <limits>
#include <vector>
#include <zlib.h>

#include "misc.h"
#include "position.h"
#include "uci.h"

namespace Stockfish {

namespace Zobrist {
extern Key side;
}

Experience experience;

namespace {

constexpr std::size_t SugaRV2FullEntrySize    = 34;
constexpr std::size_t SugaRV2MinimalEntrySize = 24;
constexpr std::size_t SugaRV2MetaBlockSize    = sizeof(std::uint32_t) * 2 + sizeof(std::uint16_t)
                                               + sizeof(float) + sizeof(std::uint64_t);

}  // namespace

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
    std::string       signatureBuf(sigV2.size(), '\0');
    in.read(signatureBuf.data(), signatureBuf.size());
    bool isV2 = signatureBuf == sigV2;
    bool isV1 = !isV2 && signatureBuf.substr(0, sigV1.size()) == sigV1;
    bool isBL = !isV1 && !isV2 && buffer.size() >= 24 && (buffer.size() % 24 == 0);
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
            struct BinBL {
                uint64_t key;
                int32_t  depth;
                int32_t  value;
                uint16_t move;
                uint16_t pad;
                int32_t  perf;
            };
            BinBL e;
            while (in.read(reinterpret_cast<char*>(&e), sizeof(e)))
            {
                int value = e.value;
                if (e.key & Zobrist::side)
                    value = -value;
                insert_entry(e.key, e.move, value, e.depth, 1);
            }
        }
        else
        {
            in.seekg(isV2 ? sigV2.size() : sigV1.size(), std::ios::beg);

            struct BinV1 {
                uint64_t key;
                uint32_t move;
                int32_t  value;
                int32_t  depth;
                uint8_t  pad[4];
            };

            struct BinV2Minimal {
                uint64_t key;
                uint32_t move;
                int32_t  value;
                int32_t  depth;
                uint16_t count;
                uint8_t  pad[2];
            };

            static_assert(sizeof(BinV2Minimal) == SugaRV2MinimalEntrySize,
                          "Unexpected minimal V2 entry size");

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

                    if (out.version != 2 || out.entrySize < SugaRV2FullEntrySize || out.entrySize > 4096)
                        return false;

                    const std::size_t metaSize  = SugaRV2MetaBlockSize;
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

                V2HeaderInfo header{};
                bool         hasHeader = parse_v2_header(header);

                std::size_t entryOffset = sigV2.size();
                std::size_t entrySize   = SugaRV2MinimalEntrySize;

                if (hasHeader)
                {
                    entryOffset += header.headerBytes;
                    entrySize = header.entrySize;
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

                        if (entrySize > SugaRV2FullEntrySize)
                            ptr += entrySize - SugaRV2FullEntrySize;

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
                    BinV2Minimal e;
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
                BinV1 e;
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
        struct BinBL {
            uint64_t key;
            int32_t  depth;
            int32_t  value;
            uint16_t move;
            uint16_t pad;
            int32_t  perf;
        };
        for (const auto& [key, vec] : table)
            for (const auto& e : vec)
            {
                BinBL be{key, e.depth, e.score, static_cast<uint16_t>(e.move.raw()), 0, e.count};
                buffer.append(reinterpret_cast<const char*>(&be), sizeof(be));
                totalMoves++;
            }
    }
    else
    {
        const std::string sig = "SugaR Experience version 2";
        buffer.append(sig);

        auto append_u8 = [&](std::uint8_t value) { buffer.push_back(static_cast<char>(value)); };
        auto append_u16 = [&](std::uint16_t value) {
            buffer.push_back(static_cast<char>(value & 0xFF));
            buffer.push_back(static_cast<char>((value >> 8) & 0xFF));
        };
        auto append_u32 = [&](std::uint32_t value) {
            for (int i = 0; i < 4; ++i)
                buffer.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
        };
        auto append_u64 = [&](std::uint64_t value) {
            for (int i = 0; i < 8; ++i)
                buffer.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
        };
        auto append_i16 = [&](std::int16_t value) { append_u16(static_cast<std::uint16_t>(value)); };
        auto append_i32 = [&](std::int32_t value) { append_u32(static_cast<std::uint32_t>(value)); };
        auto append_float = [&](float value) {
            static_assert(sizeof(float) == sizeof(std::uint32_t), "Unexpected float size");
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            append_u32(bits);
        };

        append_u8(2);  // version
        append_u64(0x103380A463E28000ULL);  // seed
        append_u32(6u);  // bucket size
        append_u32(static_cast<std::uint32_t>(SugaRV2FullEntrySize));

        const struct {
            std::uint32_t hashBits;
            std::uint32_t reserved;
            std::uint16_t endianTag;
            float         kFactor;
            std::uint64_t counters;
        } metaBlocks[2] = {{23u, 1u, 0x0002u, 11.978f, 0u}, {23u, 1u, 0x0002u, 11.978f, 0u}};

        for (const auto& meta : metaBlocks)
        {
            append_u32(meta.hashBits);
            append_u32(meta.reserved);
            append_u16(meta.endianTag);
            append_float(meta.kFactor);
            append_u64(meta.counters);
        }

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
                append_u64(key);
                append_u16(static_cast<std::uint16_t>(e.move.raw()));
                append_i16(clamp_i16(e.score));
                append_i16(clamp_i16(e.depth));
                append_i16(clamp_count(e.count));
                append_i32(0);  // wins
                append_i32(0);  // losses
                append_i32(0);  // draws
                append_i16(0);  // flags
                append_i16(0);  // age
                append_i16(0);  // pad

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
