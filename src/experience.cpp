#include "experience.h"

#include <algorithm>
#include <cctype>
#include <future>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <cstring>
#include <functional>
#include <zlib.h>

#include "misc.h"
#include "uci.h"
#include "experience_io.h"

namespace {

using namespace Stockfish;

constexpr char SUGAR_SIGNATURE[] = "SugaR Experience version 2";
constexpr std::size_t SUGAR_SIGNATURE_SIZE = sizeof(SUGAR_SIGNATURE) - 1;  // Exclude null terminator
constexpr std::uint8_t SUGAR_VERSION      = 2;
constexpr std::uint16_t SUGAR_ENDIAN_TAG = 0x0002;
constexpr float SUGAR_K_FACTOR            = 11.978f;
constexpr std::uint32_t SUGAR_BUCKET_SIZE = 6;
constexpr std::size_t SUGAR_META_BLOCKS   = 2;
constexpr std::size_t SUGAR_HEADER_SIZE   = 1 + sizeof(std::uint64_t) + sizeof(std::uint32_t) * 2;
constexpr std::size_t SUGAR_META_BLOCK_SIZE = sizeof(std::uint32_t) * 2 + sizeof(std::uint16_t)
                                              + sizeof(float) + sizeof(std::uint64_t);
constexpr std::size_t SUGAR_ENTRY_SIZE = sizeof(std::uint64_t) + sizeof(std::uint16_t) * 7
                                         + sizeof(std::int32_t) * 3;

static_assert(SUGAR_HEADER_SIZE == 17, "Unexpected SugaR header size");
static_assert(SUGAR_META_BLOCK_SIZE == 22, "Unexpected SugaR meta block size");
static_assert(SUGAR_ENTRY_SIZE == 34, "Unexpected SugaR entry size");

template <typename T>
void append_le(std::string& buffer, T value) {
    using U = std::make_unsigned_t<std::remove_cv_t<T>>;
    U v     = static_cast<U>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i)
        buffer.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

inline void append_float_le(std::string& buffer, float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t), "Unexpected float size");
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append_le(buffer, bits);
}

template <typename T>
T read_le(const std::uint8_t* data) {
    using U = std::make_unsigned_t<std::remove_cv_t<T>>;
    U result = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i)
        result |= static_cast<U>(data[i]) << (8 * i);
    return static_cast<T>(result);
}

std::uint32_t determine_hash_bits(std::size_t positions) {
    const std::size_t bucketTarget = positions ? positions : 1;
    std::size_t       buckets      = (bucketTarget + SUGAR_BUCKET_SIZE - 1) / SUGAR_BUCKET_SIZE;
    buckets                         = std::max<std::size_t>(buckets, 1);
    std::uint32_t bits              = 0;
    const auto     maxBits          = static_cast<std::uint32_t>(std::numeric_limits<std::size_t>::digits - 1);
    while (bits < maxBits && (std::size_t{1} << bits) < buckets)
        ++bits;
    return std::max<std::uint32_t>(bits, 1);
}

void append_sugar_header(std::string& buffer, std::uint32_t hashBits, std::uint64_t counters) {
    buffer.append(SUGAR_SIGNATURE, SUGAR_SIGNATURE_SIZE);
    append_le(buffer, SUGAR_VERSION);
    append_le(buffer, std::uint64_t{0});  // Seed (keep deterministic)
    append_le(buffer, SUGAR_BUCKET_SIZE);
    append_le(buffer, static_cast<std::uint32_t>(SUGAR_ENTRY_SIZE));

    for (std::size_t i = 0; i < SUGAR_META_BLOCKS; ++i)
    {
        append_le(buffer, hashBits);
        append_le(buffer, std::uint32_t{1});  // reserved
        append_le(buffer, SUGAR_ENDIAN_TAG);
        append_float_le(buffer, SUGAR_K_FACTOR);
        append_le(buffer, counters);
    }
}

bool load_sugar_entries(const std::string& buffer,
                        std::size_t offset,
                        std::size_t entrySize,
                        const std::function<void(std::uint64_t, unsigned, int, int, int)>& insert_entry)
{
    if (entrySize < SUGAR_ENTRY_SIZE)
        return false;

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(buffer.data());
    const auto   end  = buffer.size();
    if (offset > end)
        return false;

    while (offset + entrySize <= end)
    {
        const auto* entryPtr = bytes + offset;
        int         count    = read_le<std::int16_t>(entryPtr + 14);
        if (count > 0)
        {
            insert_entry(read_le<std::uint64_t>(entryPtr),
                         read_le<std::uint16_t>(entryPtr + 8),
                         static_cast<int>(read_le<std::int16_t>(entryPtr + 10)),
                         static_cast<int>(read_le<std::int16_t>(entryPtr + 12)),
                         count);
        }
        offset += entrySize;
    }

    return true;
}

}  // namespace

namespace Stockfish {

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
    totalEntries               = 0;
    totalPrunedPositions       = 0;
    totalPrunedEntries         = 0;
    totalPerPositionEvictions  = 0;
    updateCounter              = 0;
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

    const std::string sigV2 = SUGAR_SIGNATURE;
    const std::string sigV1 = "SugaR";
    std::string       header(sigV2.size(), '\0');
    in.read(header.data(), header.size());
    bool isV2 = header == sigV2;
    bool isV1 = !isV2 && header.substr(0, sigV1.size()) == sigV1;
    bool isBL = !isV1 && !isV2 && buffer.size() >= 24 && (buffer.size() % 24 == 0);
    in.clear();
    in.seekg(0, std::ios::beg);

    table.clear();
    binaryFormat              = isV1 || isV2 || isBL;
    brainLearnFormat          = isBL;
    totalEntries              = 0;
    totalPrunedPositions      = 0;
    totalPrunedEntries        = 0;
    totalPerPositionEvictions = 0;
    updateCounter             = 0;

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
                e.lastUse = ++updateCounter;
                break;
            }
        if (!dup)
        {
            vec.push_back({Move(static_cast<std::uint16_t>(move)), score, depth, count, ++updateCounter});
            ++totalEntries;
        }
    };

    if (binaryFormat)
    {
        if (isBL)
        {
            struct BinBL {
                std::uint64_t key;
                std::int32_t  depth;
                std::int32_t  value;
                std::uint16_t move;
                std::uint16_t pad;
                std::int32_t  perf;
            };
            BinBL e;
            while (in.read(reinterpret_cast<char*>(&e), sizeof(e)))
                insert_entry(e.key, e.move, e.value, e.depth, 1);
        }
        else
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(buffer.data());
            const auto   size  = buffer.size();

            if (isV2)
            {
                bool parsed = false;
                if (size >= SUGAR_SIGNATURE_SIZE + SUGAR_HEADER_SIZE)
                {
                    std::size_t offset = SUGAR_SIGNATURE_SIZE;
                    std::uint8_t version = bytes[offset];
                    offset += 1;
                    if (version == SUGAR_VERSION)
                    {
                        if (size < offset + sizeof(std::uint64_t) + sizeof(std::uint32_t) * 2)
                            offset = size;  // Force fallback
                        else
                        {
                            offset += sizeof(std::uint64_t);  // seed
                            offset += sizeof(std::uint32_t);  // bucket size
                            const auto entrySize = static_cast<std::size_t>(read_le<std::uint32_t>(bytes + offset));
                            offset += sizeof(std::uint32_t);

                            if (size >= offset)
                            {
                                std::size_t remaining  = size - offset;
                                std::size_t metaBlocks = 0;
                                if (entrySize >= SUGAR_ENTRY_SIZE && SUGAR_META_BLOCK_SIZE)
                                {
                                    const std::size_t maxBlocks = remaining / SUGAR_META_BLOCK_SIZE;
                                    for (std::size_t blocks = 0; blocks <= maxBlocks; ++blocks)
                                    {
                                        const std::size_t afterHeader = remaining - blocks * SUGAR_META_BLOCK_SIZE;
                                        if (entrySize && afterHeader % entrySize == 0)
                                        {
                                            metaBlocks = blocks;
                                            break;
                                        }
                                    }
                                }

                                if (metaBlocks * SUGAR_META_BLOCK_SIZE <= remaining)
                                {
                                    offset += metaBlocks * SUGAR_META_BLOCK_SIZE;
                                    parsed = load_sugar_entries(buffer, offset, entrySize, insert_entry);
                                }
                            }
                        }
                    }
                }

                if (!parsed)
                {
                    struct BinV2 {
                        std::uint64_t key;
                        std::uint32_t move;
                        std::int32_t  value;
                        std::int32_t  depth;
                        std::uint16_t count;
                        std::uint8_t  pad[2];
                    };

                    std::size_t offset = sigV2.size();
                    while (offset + sizeof(BinV2) <= size)
                    {
                        BinV2 e;
                        std::memcpy(&e, bytes + offset, sizeof(e));
                        insert_entry(e.key, e.move, e.value, e.depth, e.count);
                        offset += sizeof(BinV2);
                    }
                }
            }
            else
            {
                struct BinV1 {
                    std::uint64_t key;
                    std::uint32_t move;
                    std::int32_t  value;
                    std::int32_t  depth;
                    std::uint8_t  pad[4];
                };

                std::size_t offset = sigV1.size();
                while (offset + sizeof(BinV1) <= size)
                {
                    BinV1 e;
                    std::memcpy(&e, bytes + offset, sizeof(e));
                    insert_entry(e.key, e.move, e.value, e.depth, 1);
                    offset += sizeof(BinV1);
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

    enforce_limits();

    std::size_t totalPositions = table.size();
    double      frag           = totalPositions ? 100.0 * duplicateMoves / totalPositions : 0.0;

    sync_cout << "info string " << display << " -> Total moves: " << totalMoves
              << ". Total positions: " << totalPositions << ". Entries kept: " << totalEntries
              << ". Duplicate moves: " << duplicateMoves
              << ". Fragmentation: " << std::fixed << std::setprecision(2) << frag << "%)"
              << ". Approx memory: " << std::fixed << std::setprecision(2)
              << approximate_memory_usage_mib() << " MiB" << sync_endl;

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
    const std::size_t totalPositions = table.size();
    std::size_t       totalMoves     = 0;
    for (const auto& [_, vec] : table)
        totalMoves += vec.size();

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
            }
    }
    else
    {
        append_sugar_header(buffer, determine_hash_bits(totalPositions), totalMoves);

        const auto clampScore = [](int value) {
            return std::clamp(value,
                              static_cast<int>(std::numeric_limits<std::int16_t>::min()),
                              static_cast<int>(std::numeric_limits<std::int16_t>::max()));
        };
        const auto clampDepth = [](int value) {
            return std::clamp(value, 0, static_cast<int>(std::numeric_limits<std::int16_t>::max()));
        };
        const auto clampCount = [](int value) {
            return std::clamp(value, 0, static_cast<int>(std::numeric_limits<std::int16_t>::max()));
        };

        for (const auto& [key, vec] : table)
            for (const auto& e : vec)
            {
                append_le(buffer, key);
                append_le(buffer, static_cast<std::uint16_t>(e.move.raw()));
                append_le(buffer, static_cast<std::int16_t>(clampScore(e.score)));
                append_le(buffer, static_cast<std::int16_t>(clampDepth(e.depth)));
                append_le(buffer, static_cast<std::int16_t>(clampCount(e.count)));
                append_le(buffer, std::int32_t{0});
                append_le(buffer, std::int32_t{0});
                append_le(buffer, std::int32_t{0});
                append_le(buffer, std::int16_t{0});
                append_le(buffer, std::int16_t{0});
                append_le(buffer, std::int16_t{0});
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

    sync_cout << "info string " << path << " <- Total moves: " << totalMoves
              << ". Total positions: " << totalPositions << sync_endl;
}
Move Experience::probe(Position& pos, int width, int evalImportance, int minDepth, int maxMoves) {
    if (!is_ready())
        return Move::none();
    auto it = table.find(pos.key());
    if (it == table.end())
        return Move::none();

    auto& vec = it->second;
    if (vec.empty())
        return Move::none();

    const int limit = std::min<int>({maxMoves, width, static_cast<int>(vec.size())});
    if (limit <= 0)
        return Move::none();

#ifdef EXPERIENCE_PROBE_PROFILE
    const auto profileStart = std::chrono::steady_clock::now();
#endif

    const auto comparator = [&](const ExperienceEntry& a, const ExperienceEntry& b) {
        const int lhs = a.score + evalImportance * a.depth;
        const int rhs = b.score + evalImportance * b.depth;
        if (lhs != rhs)
            return lhs > rhs;
        if (a.depth != b.depth)
            return a.depth > b.depth;
        if (a.count != b.count)
            return a.count > b.count;
        return a.move.raw() < b.move.raw();
    };

#ifdef EXPERIENCE_PROBE_PROFILE
    const auto selectionStart = std::chrono::steady_clock::now();
#endif

    std::vector<ExperienceEntry> bestEntries(static_cast<std::size_t>(limit));
    std::partial_sort_copy(vec.begin(), vec.end(), bestEntries.begin(), bestEntries.end(), comparator);

#ifdef EXPERIENCE_PROBE_PROFILE
    const auto selectionEnd = std::chrono::steady_clock::now();
    const auto profileEnd   = selectionEnd;
    const auto selectionMicros =
        std::chrono::duration_cast<std::chrono::microseconds>(selectionEnd - selectionStart).count();
    const auto totalMicros =
        std::chrono::duration_cast<std::chrono::microseconds>(profileEnd - profileStart).count();
    sync_cout << "info string experience_probe entries " << vec.size()
              << " limit " << limit
              << " partial_sort_us " << selectionMicros
              << " total_us " << totalMicros << sync_endl;
#endif

    if (bestEntries.empty() || bestEntries.front().depth < minDepth)
        return Move::none();

    // Pick the best move deterministically instead of randomly.  The highest
    // ranked move represents the one with the best historical evaluation.
    const Move bestMove = bestEntries.front().move;

    for (auto& e : vec)
        if (e.move == bestMove)
        {
            e.lastUse = ++updateCounter;
            break;
        }

    return bestMove;
}

void Experience::update(Position& pos, Move move, int score, int depth) {
    if (!is_ready())
        return;
    const Key key = pos.key();
    auto       it  = table.find(key);

    if (it != table.end())
    {
        auto& vec = it->second;
        for (auto& e : vec)
            if (e.move == move)
            {
                // Update the stored statistics with a running average of the
                // evaluation.  This simple learning mechanism increases the score
                // reliability over time and retains the deepest search depth seen.
                e.score   = (e.score * e.count + score) / (e.count + 1);
                e.depth   = std::max(e.depth, depth);
                e.count++;
                e.lastUse = ++updateCounter;
                return;
            }

        // First encounter of this move in the current position.
        vec.push_back({move, score, depth, 1, ++updateCounter});
        ++totalEntries;
        prune_entries_for_position(vec);
        return;
    }

    if (maxPositionsLimit && table.size() >= maxPositionsLimit)
        prune_position_limit(true);

    auto& vec = table[key];
    vec.push_back({move, score, depth, 1, ++updateCounter});
    ++totalEntries;
    prune_entries_for_position(vec);
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

void Experience::set_limits(std::size_t maxPositions, std::size_t maxEntriesPerPosition) {
    wait_until_loaded();
    maxPositionsLimit          = maxPositions;
    maxEntriesPerPositionLimit = maxEntriesPerPosition;
    enforce_limits();
    log_prune_event("limit-update", 0, 0);
}

void Experience::enforce_limits() {
    if (!maxEntriesPerPositionLimit && !maxPositionsLimit)
        return;

    if (maxEntriesPerPositionLimit)
        for (auto it = table.begin(); it != table.end(); ++it)
            prune_entries_for_position(it->second);

    if (maxPositionsLimit && table.size() > maxPositionsLimit)
        prune_position_limit(false);
}

std::size_t Experience::prune_entries_for_position(std::vector<ExperienceEntry>& vec) {
    if (!maxEntriesPerPositionLimit || vec.size() <= maxEntriesPerPositionLimit)
        return 0;

    const auto entryLess = [](const ExperienceEntry& lhs, const ExperienceEntry& rhs) {
        if (lhs.lastUse != rhs.lastUse)
            return lhs.lastUse < rhs.lastUse;
        if (lhs.depth != rhs.depth)
            return lhs.depth < rhs.depth;
        if (lhs.count != rhs.count)
            return lhs.count < rhs.count;
        return lhs.score < rhs.score;
    };

    std::size_t removed = 0;
    while (vec.size() > maxEntriesPerPositionLimit)
    {
        auto victim = std::min_element(vec.begin(), vec.end(), entryLess);
        if (victim == vec.end())
            break;
        totalEntries -= 1;
        totalPerPositionEvictions += 1;
        vec.erase(victim);
        ++removed;
    }

    if (removed)
        log_prune_event("per-position-limit", 0, removed);

    return removed;
}

void Experience::prune_position_limit(bool reserveSlotForNewPosition) {
    if (!maxPositionsLimit)
        return;

    std::size_t target = maxPositionsLimit;
    if (reserveSlotForNewPosition && target > 0)
        --target;

    if (table.size() <= target)
        return;

    std::size_t removedPositions = 0;
    std::size_t removedEntries   = 0;

    while (table.size() > target)
    {
        auto worstIt = table.end();
        auto worstScore = std::tuple<std::uint64_t, int, int, std::size_t>{std::numeric_limits<std::uint64_t>::max(),
                                                                           std::numeric_limits<int>::max(),
                                                                           std::numeric_limits<int>::max(),
                                                                           std::numeric_limits<std::size_t>::max()};

        for (auto it = table.begin(); it != table.end(); ++it)
        {
            if (it->second.empty())
            {
                worstIt    = it;
                worstScore = {0, 0, 0, 0};
                break;
            }

            std::uint64_t newest     = 0;
            int           bestDepth  = 0;
            int           totalCount = 0;
            for (const auto& e : it->second)
            {
                newest     = std::max(newest, e.lastUse);
                bestDepth  = std::max(bestDepth, e.depth);
                totalCount += e.count;
            }

            auto candidateScore = std::tuple<std::uint64_t, int, int, std::size_t>{newest, bestDepth, totalCount,
                                                                                   it->second.size()};
            if (worstIt == table.end() || candidateScore < worstScore)
            {
                worstIt    = it;
                worstScore = candidateScore;
            }
        }

        if (worstIt == table.end())
            break;

        removedEntries += worstIt->second.size();
        totalEntries    -= worstIt->second.size();
        table.erase(worstIt);
        ++removedPositions;
        ++totalPrunedPositions;
    }

    if (removedPositions)
    {
        totalPrunedEntries += removedEntries;
        log_prune_event("position-limit", removedPositions, removedEntries);
    }
}

void Experience::log_prune_event(std::string_view reason,
                                 std::size_t     positionsRemoved,
                                 std::size_t     entriesRemoved) {
    std::ostringstream memory;
    memory << std::fixed << std::setprecision(2) << approximate_memory_usage_mib();

    sync_cout << "info string Experience prune reason=" << reason
              << " positions_removed=" << positionsRemoved
              << " entries_removed=" << entriesRemoved
              << " total_positions=" << table.size() << " total_entries=" << totalEntries
              << " max_positions=" << maxPositionsLimit
              << " max_entries_per_position=" << maxEntriesPerPositionLimit
              << " approx_memory_mib=" << memory.str()
              << " total_positions_pruned=" << totalPrunedPositions
              << " total_entries_pruned=" << totalPrunedEntries
              << " total_per_position_evictions=" << totalPerPositionEvictions << sync_endl;
}

double Experience::approximate_memory_usage_mib() const {
    constexpr double BytesPerMiB = 1024.0 * 1024.0;
    double           bytes       = static_cast<double>(totalEntries) * sizeof(ExperienceEntry);
    bytes += static_cast<double>(table.size()) * (sizeof(Key) + sizeof(std::vector<ExperienceEntry>));
    return bytes / BytesPerMiB;
}

bool Experience_InitNew(const std::string& file) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    std::string buffer;
    append_sugar_header(buffer, determine_hash_bits(0), 0);
    out.write(buffer.data(), buffer.size());
    return static_cast<bool>(out);
}

}  // namespace Stockfish
