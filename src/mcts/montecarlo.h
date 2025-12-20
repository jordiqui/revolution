#ifndef REVOLUTION_MCTS_MONTECARLO_H_INCLUDED
#define REVOLUTION_MCTS_MONTECARLO_H_INCLUDED

#include <atomic>
#include <cstddef>
#include <optional>

#include "../types.h"

namespace Revolution {

class Position;

namespace MCTS {

struct Config {
    size_t helperThreads;
    size_t strategy;
    size_t minVisits;
    bool   explore;
};

struct Result {
    Move   bestMove;
    double winRate;
    size_t visits;
    size_t iterations;
};

bool should_use_mcts(const Position& pos,
                     const Config&   cfg,
                     bool            maybeDraw,
                     int             legalMoveCount,
                     Color           us);

std::optional<Result> search(const Position& root,
                             const Config&   cfg,
                             std::atomic_bool& stopFlag,
                             Color           perspective);

}  // namespace MCTS

}  // namespace Revolution

#endif  // REVOLUTION_MCTS_MONTECARLO_H_INCLUDED
