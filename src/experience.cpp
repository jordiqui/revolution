#include "experience.h"

#include <algorithm>
#include <cctype>
#include <future>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "misc.h"

namespace Stockfish {

Experience experience;

void Experience::wait_until_loaded() const {
    if (loader.valid())
        loader.wait();
}

bool Experience::is_ready() const {
    return !loader.valid()
        || loader.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

void Experience::clear() {
    wait_until_loaded();
    table.clear();
}

void Experience::load(const std::string& file) {
    std::string   path = file;
    std::ifstream in;
    bool          convertBin = false;

    if (path.size() >= 4)
    {
        std::string ext = path.substr(path.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });

        if (ext == ".bin")
        {
            convertBin = true;
            in.open(path, std::ios::binary);
            path = path.substr(0, path.size() - 4) + ".exp";
            sync_cout << "info string '.bin' experience files are deprecated; converting to '"
                      << path << "'" << sync_endl;
        }
    }

    if (!convertBin)
        in.open(path, std::ios::binary);

    std::string display = path;
    if (path != file)
        display += " (from " + file + ")";

    if (!in)
    {
        sync_cout << "info string Could not open " << display << sync_endl;
        return;
    }

    const std::string sigV2 = "SugaR Experience version 2";
    const std::string sigV1 = "SugaR";
    std::string       header(sigV2.size(), '\0');
    in.read(header.data(), header.size());
    bool isV2 = header == sigV2;
    bool isV1 = !isV2 && header.substr(0, sigV1.size()) == sigV1;
    in.clear();
    in.seekg(0, std::ios::beg);

    table.clear();
    binaryFormat = isV1 || isV2;

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
        in.seekg(isV2 ? sigV2.size() : sigV1.size(), std::ios::beg);

        struct BinV1 {
            uint64_t key;
            uint32_t move;
            int32_t  value;
            int32_t  depth;
            uint8_t  pad[4];
        };
        struct BinV2 {
            uint64_t key;
            uint32_t move;
            int32_t  value;
            int32_t  depth;
            uint16_t count;
            uint8_t  pad[2];
        };

        if (isV2)
        {
            BinV2 e;
            while (in.read(reinterpret_cast<char*>(&e), sizeof(e)))
                insert_entry(e.key, e.move, e.value, e.depth, e.count);
        }
        else
        {
            BinV1 e;
            while (in.read(reinterpret_cast<char*>(&e), sizeof(e)))
                insert_entry(e.key, e.move, e.value, e.depth, 1);
        }
    }
    else
    {
        in.close();
        in.open(path); // reopen in text mode
        if (!in)
        {
            sync_cout << "info string Could not open " << display << sync_endl;
            return;
        }

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
    double      frag = totalPositions ? 100.0 * duplicateMoves / totalPositions : 0.0;

    sync_cout << "info string " << display << " -> Total moves: " << totalMoves
              << ". Total positions: " << totalPositions << ". Duplicate moves: " << duplicateMoves
              << ". Fragmentation: " << std::fixed << std::setprecision(2) << frag << "%)"
              << sync_endl;

    if (convertBin)
        save(path);
}

void Experience::load_async(const std::string& file) {
    loader = std::async(std::launch::async, [this, file] { load(file); });
}

void Experience::save(const std::string& file) const {
    wait_until_loaded();
    std::string path = file;

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
    }

    if (binaryFormat)
    {
        std::ofstream out(path, std::ios::binary);
        if (!out)
        {
            sync_cout << "info string Could not open " << path << " for writing" << sync_endl;
            return;
        }

        const std::string sig = "SugaR Experience version 2";
        out.write(sig.c_str(), sig.size());

        std::size_t totalMoves = 0;
        for (const auto& [key, vec] : table)
            for (const auto& e : vec)
            {
                struct BinV2 {
                    uint64_t key;
                    uint32_t move;
                    int32_t  value;
                    int32_t  depth;
                    uint16_t count;
                    uint8_t  pad[2];
                } be{key, static_cast<uint32_t>(e.move.raw()), e.score, e.depth,
                   static_cast<uint16_t>(std::min(e.count, 0xFFFF)), {0, 0}};
                out.write(reinterpret_cast<const char*>(&be), sizeof(be));
                totalMoves++;
            }

        std::size_t totalPositions = table.size();

        sync_cout << "info string " << path << " <- Total moves: " << totalMoves
                  << ". Total positions: " << totalPositions << sync_endl;
    }
    else
    {
        std::ofstream out(path);
        if (!out)
        {
            sync_cout << "info string Could not open " << path << " for writing" << sync_endl;
            return;
        }

        std::size_t totalMoves = 0;

        for (const auto& [key, vec] : table)
            for (const auto& e : vec)
            {
                out << key << ' ' << e.move.raw() << ' ' << e.score << ' ' << e.depth << ' ' << e.count
                    << '\n';
                totalMoves++;
            }

        std::size_t totalPositions = table.size();

        sync_cout << "info string " << path << " <- Total moves: " << totalMoves
                  << ". Total positions: " << totalPositions << sync_endl;
    }
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

}  // namespace Stockfish
