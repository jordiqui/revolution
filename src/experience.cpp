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
*/

#include "experience.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <random>
#include <sstream>
#include <string_view>

#include "position.h"
#include "search.h"
#include "uci.h"

namespace Stockfish {
namespace Experience {

namespace {

std::string generate_session_id() {
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::mt19937_64 rng(now ^ std::uintptr_t(&now));
    std::uniform_int_distribution<std::uint64_t> dist;
    std::ostringstream ss;
    ss << std::hex << dist(rng);
    return ss.str();
}

bool starts_with_ignore_case(const std::string& value, std::string_view prefix) {
    if (value.size() < prefix.size())
        return false;

    for (size_t i = 0; i < prefix.size(); ++i)
    {
        const auto lhs = static_cast<unsigned char>(value[i]);
        const auto rhs = static_cast<unsigned char>(prefix[i]);

        if (std::tolower(lhs) != std::tolower(rhs))
            return false;
    }

    return true;
}

}  // namespace

Manager::Manager() : session_id(generate_session_id()) {}

void Manager::set_binary_directory(std::string path) { binary_directory = std::move(path); }

void Manager::init_options(OptionsMap& options) {
    options.add("Read only learning", Option(false, [this](const Option& opt) {
                    read_only = int(opt);
                    return std::nullopt;
                }));

    options.add("Self Q-learning", Option(false, [this](const Option& opt) {
                    self_q_learning = int(opt);
                    return std::nullopt;
                }));

    options.add("Experience Book", Option(false, [this](const Option& opt) {
                    experience_book = int(opt);
                    return std::nullopt;
                }));

    options.add("Experience Book Max Moves", Option(100, 1, 100, [this](const Option& opt) {
                    book_max_moves = int(opt);
                    return std::nullopt;
                }));

    options.add("Experience Book Min Depth", Option(4, 1, 255, [this](const Option& opt) {
                    book_min_depth = int(opt);
                    return std::nullopt;
                }));

    options.add("Concurrent Experience", Option(false, [this](const Option& opt) {
                    bool newValue = int(opt);
                    if (newValue != concurrent_mode)
                    {
                        concurrent_mode = newValue;
                        load();
                    }
                    return std::nullopt;
                }));

    read_only       = int(options["Read only learning"]);
    self_q_learning = int(options["Self Q-learning"]);
    experience_book = int(options["Experience Book"]);
    book_max_moves  = int(options["Experience Book Max Moves"]);
    book_min_depth  = int(options["Experience Book Min Depth"]);
    concurrent_mode = int(options["Concurrent Experience"]);
}

void Manager::load() {
    {
        std::lock_guard<std::mutex> lk(mutex);
        table.clear();
        dirty                = false;
        game_learning_active = true;
    }

    load_file(base_file());

    if (concurrent_mode)
        load_file(primary_file());

    merge_staged_files();
}

void Manager::on_new_game() {
    std::lock_guard<std::mutex> lk(mutex);
    game_learning_active = true;
}

bool Manager::should_learn(const Position& pos) const {
    if (read_only)
        return false;

    if (!game_learning_active)
    {
        if (pos.count<ALL_PIECES>() > 8)
            return false;
    }

    return true;
}

std::optional<BookSuggestion> Manager::best_book_move(const Position& pos) const {
    if (!experience_book)
        return std::nullopt;

    std::lock_guard<std::mutex> lk(mutex);
    auto                         it = table.find(pos.key());
    if (it == table.end())
        return std::nullopt;

    std::vector<MoveData> candidates;
    candidates.reserve(it->second.size());

    for (const auto& data : it->second)
        if (data.depth >= book_min_depth)
            candidates.push_back(data);

    if (candidates.empty())
        return std::nullopt;

    std::sort(candidates.begin(), candidates.end(), [](const MoveData& lhs, const MoveData& rhs) {
        if (lhs.performance != rhs.performance)
            return lhs.performance > rhs.performance;
        if (lhs.score != rhs.score)
            return lhs.score > rhs.score;
        if (lhs.depth != rhs.depth)
            return lhs.depth > rhs.depth;
        return lhs.visits > rhs.visits;
    });

    if (book_max_moves > 0 && candidates.size() > static_cast<size_t>(book_max_moves))
        candidates.resize(static_cast<size_t>(book_max_moves));

    BookSuggestion suggestion;
    suggestion.move = candidates.front().move;
    suggestion.info = describe_moves(candidates, pos);

    return suggestion;
}

void Manager::record_result(const Position& pos, const Search::RootMove& rootMove, Depth depth) {
    if (depth < 4)
        return;

    if (rootMove.pv.empty())
        return;

    const Move bestMove = rootMove.pv.front();

    if (bestMove == Move::none())
        return;

    if (!should_learn(pos))
        return;

    MoveData updated;
    updated.move          = bestMove;
    updated.depth         = depth;
    updated.score         = rootMove.score;
    updated.averageScore  = rootMove.averageScore == -VALUE_INFINITE ? rootMove.score : rootMove.averageScore;
    updated.performance   = calculate_performance(updated.score);
    updated.visits        = 1;

    std::unique_lock<std::mutex> lk(mutex);
    auto&                        bucket = table[pos.key()];
    auto                         it = std::find_if(bucket.begin(), bucket.end(), [&](const MoveData& m) {
        return m.move == bestMove;
    });

    if (it == bucket.end())
    {
        bucket.push_back(updated);
    }
    else
    {
        it->depth = std::max(it->depth, updated.depth);

        if (self_q_learning)
        {
            it->score        = Value(it->score + (updated.score - it->score) / 2);
            it->averageScore = Value(it->averageScore + (updated.averageScore - it->averageScore) / 2);
        }
        else
        {
            it->score        = updated.score;
            it->averageScore = updated.averageScore;
        }

        it->performance = calculate_performance(it->score);
        ++it->visits;
    }

    dirty = true;
    lk.unlock();

    flush();
}

std::string Manager::describe_moves(const std::vector<MoveData>& moves, const Position& pos) const {
    std::ostringstream ss;
    ss << "experience";

    bool first = true;
    for (const auto& data : moves)
    {
        if (!first)
            ss << " |";

        first = false;
        ss << ' ' << UCIEngine::move(data.move, pos.is_chess960()) << " (d" << data.depth << ", sc "
           << data.score << ", perf " << data.performance << ", visits " << data.visits << ')';
    }

    return ss.str();
}

std::string Manager::describe_position(const Position& pos) const {
    std::lock_guard<std::mutex> lk(mutex);
    auto                         it = table.find(pos.key());
    if (it == table.end() || it->second.empty())
        return "experience empty";

    auto moves = it->second;
    std::sort(moves.begin(), moves.end(), [](const MoveData& lhs, const MoveData& rhs) {
        if (lhs.performance != rhs.performance)
            return lhs.performance > rhs.performance;
        if (lhs.depth != rhs.depth)
            return lhs.depth > rhs.depth;
        return lhs.visits > rhs.visits;
    });

    return describe_moves(moves, pos);
}

std::string Manager::quick_reset() {
    size_t updated = 0;

    {
        std::lock_guard<std::mutex> lk(mutex);

        for (auto& [key, moves] : table)
            for (auto& data : moves)
            {
                data.performance = calculate_performance(data.score);
                ++updated;
            }

        if (updated == 0)
            return "experience empty";

        dirty = true;
    }

    flush();

    std::ostringstream ss;
    ss << "experience recalculated " << updated << " moves";
    return ss.str();
}

void Manager::flush() {
    PositionTable snapshot;
    {
        std::lock_guard<std::mutex> lk(mutex);
        if (!dirty)
            return;

        snapshot = table;
        dirty    = false;
    }

    auto target = primary_file();
    if (target.empty())
        return;

    write_snapshot(target, snapshot);
}

void Manager::flush_to(const std::filesystem::path& path) const {
    if (path.empty())
        return;

    PositionTable snapshot;
    {
        std::lock_guard<std::mutex> lk(mutex);
        snapshot = table;
    }

    write_snapshot(path, snapshot);
}

void Manager::write_snapshot(const std::filesystem::path& path, const PositionTable& snapshot) const {
    namespace fs = std::filesystem;

    if (path.empty())
        return;

    std::error_code ec;
    const auto       dir = path.parent_path();
    if (!dir.empty())
        fs::create_directories(dir, ec);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return;

    out << "REVOLUTION_EXP 1\n";

    std::vector<std::pair<Key, std::vector<MoveData>>> ordered(snapshot.begin(), snapshot.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });

    for (const auto& [key, moves] : ordered)
    {
        auto sortedMoves = moves;
        std::sort(sortedMoves.begin(), sortedMoves.end(), [](const MoveData& lhs, const MoveData& rhs) {
            if (lhs.performance != rhs.performance)
                return lhs.performance > rhs.performance;
            if (lhs.score != rhs.score)
                return lhs.score > rhs.score;
            if (lhs.depth != rhs.depth)
                return lhs.depth > rhs.depth;
            return lhs.visits > rhs.visits;
        });

        for (const auto& data : sortedMoves)
            out << key << ' ' << data.move.raw() << ' ' << data.depth << ' ' << data.score << ' '
                << data.averageScore << ' ' << data.performance << ' ' << data.visits << '\n';
    }
}

void Manager::load_file(const std::filesystem::path& path) {
    if (path.empty())
        return;

    std::ifstream in(path);
    if (!in)
        return;

    std::string header;
    std::streampos dataStart = in.tellg();
    if (std::getline(in, header))
    {
        if (header.rfind("REVOLUTION_EXP", 0) != 0)
            in.seekg(dataStart);
    }

    std::vector<std::pair<Key, MoveData>> parsed;
    std::string                            line;
    while (std::getline(in, line))
    {
        if (line.empty())
            continue;

        std::istringstream iss(line);
        std::uint64_t       rawKey;
        unsigned            rawMove;
        int                 depth;
        int                 score;
        int                 average;
        int                 performance;
        std::size_t         visits = 1;

        if (!(iss >> rawKey >> rawMove >> depth >> score >> average >> performance))
            continue;

        iss >> visits;

        Move move(static_cast<std::uint16_t>(rawMove));
        if (!move.is_ok())
            continue;

        MoveData data;
        data.move          = move;
        data.depth         = depth;
        data.score         = score;
        data.averageScore  = average;
        data.performance   = performance;
        data.visits        = std::max<std::size_t>(1, visits);

        parsed.emplace_back(static_cast<Key>(rawKey), data);
    }

    if (parsed.empty())
        return;

    std::lock_guard<std::mutex> lk(mutex);

    for (const auto& entry : parsed)
    {
        const Key        key  = entry.first;
        const MoveData&  data = entry.second;
        auto&            bucket = table[key];
        auto             it     = std::find_if(bucket.begin(), bucket.end(), [&](const MoveData& existing) {
            return existing.move == data.move;
        });

        if (it == bucket.end())
            bucket.push_back(data);
        else
        {
            it->depth        = std::max(it->depth, data.depth);
            it->score        = std::max(it->score, data.score);
            it->averageScore = std::max(it->averageScore, data.averageScore);
            it->performance  = std::max(it->performance, data.performance);
            it->visits += data.visits;
        }
    }
}

void Manager::merge_staged_files() {
    namespace fs = std::filesystem;

    if (binary_directory.empty())
        return;

    std::error_code ec;
    if (!fs::exists(binary_directory, ec))
        return;

    std::vector<fs::path> staged;
    const auto            base = base_file();

    for (const auto& entry : fs::directory_iterator(binary_directory, ec))
    {
        if (ec)
            break;

        if (!entry.is_regular_file())
            continue;

        const auto path = entry.path();
        if (path == base)
            continue;
        if (path.extension() != ".exp")
            continue;

        const auto stem = path.stem().string();
        if (!starts_with_ignore_case(stem, "experience"))
            continue;

        if (concurrent_mode && !session_id.empty() && stem == ("experience." + session_id))
            continue;

        staged.push_back(path);
    }

    if (staged.empty())
        return;

    for (const auto& path : staged)
        load_file(path);

    {
        std::lock_guard<std::mutex> lk(mutex);
        dirty = true;
    }

    flush_to(base);

    for (const auto& path : staged)
        fs::remove(path, ec);
}

std::filesystem::path Manager::base_file() const {
    namespace fs = std::filesystem;

    if (binary_directory.empty())
        return {};

    fs::path path(binary_directory);
    path /= "experience.exp";
    return path;
}

std::filesystem::path Manager::primary_file() const {
    if (!concurrent_mode)
        return base_file();

    if (binary_directory.empty())
        return {};

    std::filesystem::path path(binary_directory);
    path /= "experience." + session_id + ".exp";
    return path;
}

int Manager::calculate_performance(Value score) const {
    constexpr double scale = 600.0;
    const double      probability = 1.0 / (1.0 + std::exp(-double(score) / scale));
    return static_cast<int>(std::round(probability * 1000.0));
}

}  // namespace Experience
}  // namespace Stockfish

