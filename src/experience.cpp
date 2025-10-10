#include "experience.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <zlib.h>

#include "experience_io.h"
#include "misc.h"
#include "uci.h"

namespace {

constexpr const char kSigV2[] = "SugaR Experience version 2";
constexpr std::size_t kSigV2Len = sizeof(kSigV2) - 1;
constexpr std::uint16_t kLittleEndianTag = 0x0002;
constexpr std::size_t   kMetaBlockDefaultCount = 2;

#pragma pack(push, 1)
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

static_assert(sizeof(EntryV2) == 34, "EntryV2 must match the SugaR layout");

HeaderV2 make_default_header(std::uint32_t entrySize) {
    using clock    = std::chrono::high_resolution_clock;
    auto now       = clock::now().time_since_epoch().count();
    HeaderV2 hdr{};
    hdr.version     = 2;
    hdr.seed        = static_cast<std::uint64_t>(now);
    hdr.bucket_size = 6;
    hdr.entry_size  = entrySize;
    return hdr;
}

MetaBlockV2 make_default_meta() {
    MetaBlockV2 meta{};
    meta.hash_bits  = 23;
    meta.reserved   = 1;
    meta.endian_tag = kLittleEndianTag;
    meta.k_factor   = 11.978f;
    meta.counters   = 0;
    return meta;
}

std::vector<char> default_header_bytes(std::uint32_t entrySize) {
    const HeaderV2   header   = make_default_header(entrySize);
    const MetaBlockV2 meta    = make_default_meta();
    const std::size_t headerBytes = sizeof(header) + kMetaBlockDefaultCount * sizeof(meta);

    std::vector<char> bytes(headerBytes);
    std::memcpy(bytes.data(), &header, sizeof(header));
    for (std::size_t i = 0; i < kMetaBlockDefaultCount; ++i)
        std::memcpy(bytes.data() + sizeof(header) + i * sizeof(meta), &meta, sizeof(meta));
    return bytes;
}

std::size_t determine_meta_blocks(std::size_t fileSize, std::size_t sigSize, const HeaderV2& header) {
    const std::size_t headerBasic   = sizeof(header);
    if (fileSize < sigSize + headerBasic)
        return 0;

    const std::size_t headerRemaining = fileSize - (sigSize + headerBasic);
    const std::size_t metaBlockSize   = sizeof(MetaBlockV2);
    if (!header.entry_size)
        return 0;

    for (std::size_t blocks = 1; blocks <= headerRemaining / metaBlockSize; ++blocks)
    {
        const std::size_t afterHeader = headerRemaining - blocks * metaBlockSize;
        if (afterHeader % header.entry_size == 0)
            return blocks;
    }

    return 0;
}

std::int16_t clamp_score(int value) {
    return static_cast<std::int16_t>(std::clamp(value, -32768, 32767));
}

std::int16_t sanitize_count(int value) {
    value = std::clamp(value, 1, 32767);
    return static_cast<std::int16_t>(value);
}

int decode_count(const EntryV2& entry) {
    int count = entry.count;
    if (count <= 0)
    {
        const long total = static_cast<long>(entry.wins) + entry.losses + entry.draws;
        if (total > 0)
            count = static_cast<int>(std::min<long>(total, std::numeric_limits<int>::max()));
    }
    return std::max(count, 1);
}

}  // namespace

namespace Stockfish {

Experience experience;

bool Experience_InitNew(const std::string& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    const auto headerBytes = default_header_bytes(sizeof(EntryV2));
    out.write(kSigV2, kSigV2Len);
    out.write(headerBytes.data(), static_cast<std::streamsize>(headerBytes.size()));
    return static_cast<bool>(out);
}

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
    std::string       header(sigV2.size(), '\0');
    in.read(header.data(), header.size());
    bool isV2 = header == sigV2;
    bool isV1 = !isV2 && header.substr(0, sigV1.size()) == sigV1;
    bool isBL = !isV1 && !isV2 && buffer.size() >= 24 && (buffer.size() % 24 == 0);
    in.clear();
    in.seekg(0, std::ios::beg);

    table.clear();
    binaryFormat     = isV1 || isV2 || isBL;
    brainLearnFormat = isBL;
    v2HeaderBytes.clear();
    v2EntrySize = 0;

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
                insert_entry(e.key, e.move, e.value, e.depth, 1);
        }
        else if (isV2)
        {
            in.seekg(static_cast<std::streamoff>(kSigV2Len), std::ios::beg);

            HeaderV2 header{};
            if (!in.read(reinterpret_cast<char*>(&header), sizeof(header)))
            {
                sync_cout << "info string Malformed experience header in " << display << sync_endl;
                return;
            }

            if (!header.entry_size)
                header.entry_size = static_cast<std::uint32_t>(sizeof(EntryV2));

            v2EntrySize = header.entry_size;
            const std::size_t metaBlocks = determine_meta_blocks(buffer.size(), kSigV2Len, header);
            const std::size_t metaBytes  = metaBlocks * sizeof(MetaBlockV2);

            v2HeaderBytes.resize(sizeof(header) + metaBytes);
            std::memcpy(v2HeaderBytes.data(), &header, sizeof(header));

            for (std::size_t i = 0; i < metaBlocks; ++i)
            {
                MetaBlockV2 meta{};
                if (!in.read(reinterpret_cast<char*>(&meta), sizeof(meta)))
                {
                    v2HeaderBytes.resize(sizeof(header) + i * sizeof(meta));
                    break;
                }
                std::memcpy(v2HeaderBytes.data() + sizeof(header) + i * sizeof(meta), &meta, sizeof(meta));
            }

            const std::size_t entryOffset = kSigV2Len + sizeof(header) + metaBytes;
            in.clear();
            in.seekg(static_cast<std::streamoff>(entryOffset), std::ios::beg);

            const std::size_t entrySize = v2EntrySize ? v2EntrySize : sizeof(EntryV2);
            if (entrySize)
            {
                std::vector<char> entry(entrySize);

                struct LegacyEntry {
                    uint64_t key;
                    uint32_t move;
                    int32_t  value;
                    int32_t  depth;
                    uint16_t count;
                    uint8_t  pad[2];
                };

                while (in.read(entry.data(), static_cast<std::streamsize>(entry.size())))
                {
                    if (entry.size() >= sizeof(EntryV2))
                    {
                        EntryV2 e{};
                        std::memcpy(&e, entry.data(), sizeof(e));
                        insert_entry(e.key,
                                     e.move,
                                     e.score,
                                     e.depth,
                                     decode_count(e));
                    }
                    else if (entry.size() >= sizeof(LegacyEntry))
                    {
                        LegacyEntry e{};
                        std::memcpy(&e, entry.data(), sizeof(e));
                        int count = e.count ? e.count : 1;
                        insert_entry(e.key, e.move, e.value, e.depth, count);
                    }
                }
            }
        }
        else
        {
            in.seekg(static_cast<std::streamoff>(sigV1.size()), std::ios::beg);

            struct BinV1 {
                uint64_t key;
                uint32_t move;
                int32_t  value;
                int32_t  depth;
                uint8_t  pad[4];
            };

            BinV1 e;
            while (in.read(reinterpret_cast<char*>(&e), sizeof(e)))
                insert_entry(e.key, e.move, e.value, e.depth, 1);
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
            std::string        keyStr, moveStr;
            int                score, depth, count;

            if (!(iss >> keyStr >> moveStr >> score >> depth >> count))
                continue;

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
        std::vector<char> headerBytes = v2HeaderBytes;
        const std::size_t minHeaderSize = sizeof(HeaderV2) + sizeof(MetaBlockV2);
        if (headerBytes.size() < minHeaderSize)
            headerBytes = default_header_bytes(static_cast<std::uint32_t>(sizeof(EntryV2)));

        HeaderV2 header{};
        std::memcpy(&header, headerBytes.data(), std::min(headerBytes.size(), sizeof(header)));

        const std::uint32_t desiredEntrySize = std::max<std::uint32_t>(header.entry_size,
                                                                        static_cast<std::uint32_t>(sizeof(EntryV2)));
        if (!header.entry_size || header.entry_size != desiredEntrySize)
        {
            header.entry_size = desiredEntrySize;
            if (headerBytes.size() >= sizeof(header))
                std::memcpy(headerBytes.data(), &header, sizeof(header));
            else
                headerBytes = default_header_bytes(desiredEntrySize);
        }

        std::size_t entryCount = 0;
        for (const auto& kv : table)
            entryCount += kv.second.size();

        buffer.reserve(buffer.size() + kSigV2Len + headerBytes.size() + entryCount * header.entry_size);
        buffer.append(kSigV2, kSigV2Len);
        buffer.append(headerBytes.data(), headerBytes.size());

        std::vector<char> entryBytes(header.entry_size, 0);
        for (const auto& [key, vec] : table)
            for (const auto& e : vec)
            {
                EntryV2 out{};
                out.key   = key;
                out.move  = static_cast<std::uint16_t>(e.move.raw());
                out.score = clamp_score(e.score);
                out.depth = clamp_score(e.depth);
                out.count = sanitize_count(e.count);
                out.wins  = 0;
                out.losses = 0;
                out.draws  = out.count;
                out.flags  = 0;
                out.age    = 0;
                out.pad    = 0;

                std::fill(entryBytes.begin(), entryBytes.end(), 0);
                std::memcpy(entryBytes.data(), &out, std::min(sizeof(out), entryBytes.size()));
                buffer.append(entryBytes.data(), entryBytes.size());
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
Move Experience::probe(Position& pos, int width, int evalImportance, int minDepth, int maxMoves) {
    if (!is_ready())
        return Move::none();
    auto it = table.find(pos.key());
    if (it == table.end())
        return Move::none();

    auto vec = it->second;
    if (vec.empty())
        return Move::none();

    // Order moves by their historical evaluation and depth so that the most
    // promising moves come first.  This allows the engine to "learn" from
    // previous games by preferring moves with the best average score at the
    // deepest search.
    std::sort(vec.begin(), vec.end(), [&](const ExperienceEntry& a, const ExperienceEntry& b) {
        return (a.score + evalImportance * a.depth) > (b.score + evalImportance * b.depth);
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
    auto& vec = table[pos.key()];
    for (auto& e : vec)
        if (e.move == move)
        {
            // Update the stored statistics with a running average of the
            // evaluation.  This simple learning mechanism increases the score
            // reliability over time and retains the deepest search depth seen.
            e.score = (e.score * e.count + score) / (e.count + 1);
            e.depth = std::max(e.depth, depth);
            e.count++;
            return;
        }
    // First encounter of this move in the current position.
    vec.push_back({move, score, depth, 1});
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
    auto vec = it->second;
    std::sort(vec.begin(), vec.end(), [&](const ExperienceEntry& a, const ExperienceEntry& b) {
        return (a.score + evalImportance * a.depth) > (b.score + evalImportance * b.depth);
    });
    int shown = 0;
    for (const auto& e : vec)
    {
        if (shown++ >= maxMoves)
            break;
        sync_cout << "info string " << UCIEngine::move(e.move, pos.is_chess960()) << " score "
                  << e.score << " depth " << e.depth << " count " << e.count << sync_endl;
    }
}

}  // namespace Stockfish
