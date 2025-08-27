/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2025 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  Modifications Copyright (C) 2024 Jorge Ruiz Centelles
*/

#include "experience.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "misc.h"

namespace Stockfish {

Experience experience;

void Experience::clear() { table.clear(); }

void Experience::load(const std::string& file) {
    std::string path = file;

    if (path.size() >= 4)
    {
        std::string ext = path.substr(path.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return char(std::tolower(c));
        });

        if (ext == ".bin")
        {
            path = path.substr(0, path.size() - 4) + ".exp";
            sync_cout << "info string '.bin' experience files are deprecated; trying '" << path
                      << "'" << sync_endl;
        }
    }

    std::ifstream in(path);
    if (!in)
    {
        sync_cout << "info string Could not open " << path << sync_endl;
        return;
    }

    table.clear();

    uint64_t key;
    unsigned move;
    int      score, depth, count;

    std::size_t totalMoves = 0;
    std::size_t duplicateMoves = 0;

    while (in >> key >> move >> score >> depth >> count)
    {
        totalMoves++;
        auto& vec  = table[key];
        bool  dup  = false;
        for (auto& e : vec)
            if (e.move.raw() == move)
            {
                dup        = true;
                duplicateMoves++;
                e.score = score;
                e.depth = depth;
                e.count += count;
                break;
            }
        if (!dup)
            vec.push_back({Move(static_cast<std::uint16_t>(move)), score, depth, count});
    }

    std::size_t totalPositions = table.size();
    double      frag = totalPositions ? 100.0 * duplicateMoves / totalPositions : 0.0;

    sync_cout << "info string " << path << " -> Total moves: " << totalMoves
              << ". Total positions: " << totalPositions
              << ". Duplicate moves: " << duplicateMoves
              << ". Fragmentation: " << std::fixed << std::setprecision(2) << frag << "%)"
              << sync_endl;
}

void Experience::save(const std::string& file) const {
    std::ofstream out(file);
    if (!out)
        return;
    for (const auto& [key, vec] : table)
        for (const auto& e : vec)
            out << key << ' ' << e.move.raw() << ' ' << e.score << ' ' << e.depth << ' ' << e.count
                << '\n';
}

Move Experience::probe(Position& pos, [[maybe_unused]] int width,
                       int evalImportance, int minDepth, int maxMoves) {
    auto it = table.find(pos.key());
    if (it == table.end())
        return Move::none();

    auto vec = it->second;
    if (vec.empty())
        return Move::none();

    std::sort(vec.begin(), vec.end(), [&](const ExperienceEntry& a, const ExperienceEntry& b) {
        return (a.score + evalImportance * a.depth) > (b.score + evalImportance * b.depth);
    });

    vec.resize(std::min<int>(maxMoves, static_cast<int>(vec.size())));
    const auto& best = vec.front();
    if (best.depth < minDepth)
        return Move::none();

    return best.move;
}

void Experience::update(Position& pos, Move move, int score, int depth) {
    auto& vec = table[pos.key()];
    for (auto& e : vec)
        if (e.move == move)
        {
            e.score = score;
            e.depth = depth;
            e.count++;
            return;
        }
    vec.push_back({move, score, depth, 1});
}

}  // namespace Stockfish
