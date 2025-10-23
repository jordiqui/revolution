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

#ifndef EXPERIENCE_H_INCLUDED
#define EXPERIENCE_H_INCLUDED

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "types.h"
#include "ucioption.h"

namespace Stockfish {

class Position;

namespace Search {
struct RootMove;
}  // namespace Search

namespace Experience {

struct MoveData {
    Move   move = Move::none();
    Depth  depth = 0;
    Value  score = 0;
    Value  averageScore = 0;
    int    performance = 0;
    size_t visits = 0;
};

struct BookSuggestion {
    Move        move = Move::none();
    std::string info;
};

class Manager {
   public:
    Manager();

    void set_binary_directory(std::string path);
    void init_options(OptionsMap& options);
    void load();
    void on_new_game();

    std::optional<BookSuggestion> best_book_move(const Position& pos) const;
    void                          record_result(const Position& pos,
                                                const Search::RootMove& rootMove,
                                                Depth depth);
    std::string                   describe_position(const Position& pos) const;
    std::string                   quick_reset();
    void                          flush();

   private:
    using PositionTable = std::unordered_map<Key, std::vector<MoveData>>;

    bool should_learn(const Position& pos) const;

    void load_file(const std::filesystem::path& path);
    void merge_staged_files();
    void flush_to(const std::filesystem::path& path) const;
    void write_snapshot(const std::filesystem::path& path, const PositionTable& snapshot) const;
    std::filesystem::path base_file() const;
    std::filesystem::path primary_file() const;
    int                   calculate_performance(Value score) const;
    std::string           describe_moves(const std::vector<MoveData>& moves, const Position& pos) const;

    std::string session_id;
    std::string binary_directory;

    mutable std::mutex mutex;
    PositionTable       table;

    bool read_only       = false;
    bool self_q_learning = false;
    bool experience_book = true;
    bool concurrent_mode = false;
    int  book_max_moves  = 100;
    int  book_min_depth  = 4;

    bool game_learning_active = true;
    bool dirty                = false;
};

}  // namespace Experience

}  // namespace Stockfish

#endif  // #ifndef EXPERIENCE_H_INCLUDED

