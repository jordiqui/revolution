#include "experience.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace Stockfish {

Experience experience;

void Experience::clear() { table.clear(); }

void Experience::load(const std::string& file) {
    std::ifstream in(file);
    if (!in)
        return;
    table.clear();
    uint64_t key;
    unsigned move;
    int      score, depth, count;
    while (in >> key >> move >> score >> depth >> count)
        table[key].push_back({Move(static_cast<std::uint16_t>(move)), score, depth, count});
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
