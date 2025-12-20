#include "montecarlo.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iterator>
#include <limits>
#include <memory>
#include <random>
#include <utility>
#include <vector>

#include "../bitboard.h"
#include "../evaluate.h"
#include "../movegen.h"
#include "../position.h"

namespace Revolution {
namespace {

constexpr double kBaseExploration = 0.85;
constexpr double kExplorationScale = 0.0125;
constexpr size_t kBaseIterations   = 400;
constexpr size_t kIterationScale   = 18;
constexpr size_t kMaxPlayoutDepth  = 12;
constexpr double kEvalScale        = 220.0;

struct Node {
    explicit Node(Node* parent_, Move m, const Position& pos) :
        parent(parent_),
        move(m),
        totalValue(0.0),
        visits(0) {
        for (const Move moveCandidate : MoveList<LEGAL>(pos))
            untriedMoves.push_back(moveCandidate);
    }

    bool has_untried_moves() const { return !untriedMoves.empty(); }
    bool has_children() const { return !children.empty(); }

    Node*                              parent;
    Move                               move;
    double                             totalValue;
    size_t                             visits;
    std::vector<Move>                  untriedMoves;
    std::vector<std::unique_ptr<Node>> children;
};

std::mt19937& rng() {
    thread_local std::mt19937 engine(std::random_device{}());
    return engine;
}

Move pop_random_move(std::vector<Move>& moves) {
    std::uniform_int_distribution<size_t> dist(0, moves.size() - 1);
    size_t                                idx = dist(rng());
    Move                                  chosen = moves[idx];
    moves[idx] = moves.back();
    moves.pop_back();
    return chosen;
}

double logistic_from_eval(int eval) {
    return 1.0 / (1.0 + std::exp(-static_cast<double>(eval) / kEvalScale));
}

bool king_in_danger(const Position& pos, Color color) {
    const Square   ksq       = pos.square<KING>(color);
    Bitboard       attackers = pos.attackers_to(ksq) & pos.pieces(~color);
    if (!attackers)
        return false;

    int dangerScore = 0;
    while (attackers)
    {
        const Square s        = pop_lsb(attackers);
        const Piece  attacker = pos.piece_on(s);
        switch (type_of(attacker))
        {
        case QUEEN:
        case ROOK:
            dangerScore += 3;
            break;
        case BISHOP:
        case KNIGHT:
            dangerScore += 2;
            break;
        default:
            dangerScore += 1;
            break;
        }

        if (dangerScore >= 6)
            return true;
    }

    return dangerScore >= 4;
}

struct PositionMetrics {
    int  totalPieces;
    int  majorMinorPieces;
    int  pawns;
    int  mobility;
    bool usKingDanger;
    bool themKingDanger;
};

PositionMetrics collect_metrics(const Position& pos, Color us) {
    PositionMetrics metrics{};
    metrics.totalPieces      = pos.count<ALL_PIECES>();
    metrics.pawns            = pos.count<PAWN>();
    metrics.majorMinorPieces = metrics.totalPieces - metrics.pawns - 2;  // remove kings
    metrics.mobility         = int(MoveList<LEGAL>(pos).size());
    metrics.usKingDanger     = king_in_danger(pos, us);
    metrics.themKingDanger   = king_in_danger(pos, ~us);
    return metrics;
}

bool is_petrosian_high(const PositionMetrics& m) {
    return m.majorMinorPieces >= 8 && m.mobility <= 40 && m.mobility >= 12 && m.pawns >= 6
        && !m.usKingDanger;
}

bool is_petrosian_middle(const PositionMetrics& m) {
    return m.majorMinorPieces >= 6 && m.majorMinorPieces <= 10 && m.mobility >= 10
        && m.mobility <= 36 && !m.usKingDanger;
}

bool is_tal_high(const PositionMetrics& m) {
    return m.mobility >= 34 && (m.usKingDanger || m.themKingDanger);
}

bool is_capablanca(const PositionMetrics& m) {
    return m.mobility <= 18 && !m.usKingDanger && !m.themKingDanger && m.majorMinorPieces >= 5;
}

class MonteCarloImpl {
   public:
    MonteCarloImpl(const Position&     root,
                   Color               perspective,
                   const MCTS::Config& cfg,
                   std::atomic_bool&   stopFlag) :
        rootColor(perspective),
        config(cfg),
        stop(stopFlag),
        rootFen(root.fen()),
        rootIsChess960(root.is_chess960()) {}

    std::optional<MCTS::Result> run();

   private:
    double playout(Position& pos);
    Node*  select_child(Node& node, double exploration) const;

    Color               rootColor;
    const MCTS::Config& config;
    std::atomic_bool&   stop;
    std::string        rootFen;
    bool               rootIsChess960;
};

std::optional<MCTS::Result> MonteCarloImpl::run() {
    StateInfo rootState;
    Position  rootPosition;
    rootPosition.set(rootFen, rootIsChess960, &rootState);

    if (MoveList<LEGAL>(rootPosition).size() == 0)
        return std::nullopt;

    Node root(nullptr, Move::none(), rootPosition);

    const size_t helperCount    = std::max<size_t>(1, config.helperThreads + 1);
    const double exploration    = kBaseExploration + kExplorationScale * double(config.strategy);
    const size_t iterationLimit = (kBaseIterations + kIterationScale * config.strategy) * helperCount;
    const size_t targetVisits   = std::max<size_t>(iterationLimit,
                                                 (config.minVisits > 0 ? config.minVisits : 1)
                                                   * std::max<size_t>(1, root.untriedMoves.size()));

    size_t iterations = 0;
    auto   start      = std::chrono::steady_clock::now();
    const auto maxDuration = std::chrono::milliseconds(40 + 4 * config.strategy);

    std::array<StateInfo, MAX_PLY + kMaxPlayoutDepth + 4> stateBuffer{};

    while (!stop.load(std::memory_order_relaxed))
    {
        if (iterations >= targetVisits)
            break;

        if (iterations >= iterationLimit)
        {
            auto now = std::chrono::steady_clock::now();
            if (now - start >= maxDuration)
                break;
        }

        StateInfo iterationRootState;
        Position  current;
        current.set(rootFen, rootIsChess960, &iterationRootState);
        Node*  node = &root;
        size_t ply  = 0;

        std::vector<Node*> path;
        path.reserve(32);
        path.push_back(node);

        // Selection
        while (!node->has_untried_moves() && node->has_children())
        {
            node = select_child(*node, exploration);
            path.push_back(node);

            current.do_move(node->move, stateBuffer[ply]);
            ++ply;

            if (ply >= kMaxPlayoutDepth)
                break;
        }

        // Expansion
        if (node->has_untried_moves() && ply < kMaxPlayoutDepth)
        {
            Move next = pop_random_move(node->untriedMoves);
            current.do_move(next, stateBuffer[ply]);
            ++ply;
            auto child = std::make_unique<Node>(node, next, current);
            Node* childPtr = child.get();
            node->children.emplace_back(std::move(child));
            node = childPtr;
            path.push_back(node);
        }

        double reward = playout(current);

        for (Node* traversed : path)
        {
            traversed->visits += 1;
            traversed->totalValue += reward;
        }

        ++iterations;
    }

    if (root.children.empty())
        return std::nullopt;

    auto bestChildIt = std::max_element(root.children.begin(), root.children.end(), [](const auto& a, const auto& b) {
        return a->visits < b->visits;
    });

    const Node* bestChild = bestChildIt->get();
    if (!bestChild || bestChild->visits == 0)
        return std::nullopt;

    double meanValue = bestChild->totalValue / double(bestChild->visits);
    MCTS::Result result{bestChild->move, meanValue, bestChild->visits, iterations};
    return result;
}

Node* MonteCarloImpl::select_child(Node& node, double exploration) const {
    Node*  best      = nullptr;
    double bestScore = -std::numeric_limits<double>::infinity();
    double logParent = std::log(double(node.visits) + 1.0);

    for (auto& childPtr : node.children)
    {
        Node* child = childPtr.get();
        if (!child || child->visits == 0)
            continue;

        double exploitation = child->totalValue / double(child->visits);
        double explorationTerm = exploration * std::sqrt(logParent / double(child->visits));
        double score           = exploitation + explorationTerm;

        if (score > bestScore)
        {
            best      = child;
            bestScore = score;
        }
    }

    if (!best)
        best = node.children.front().get();

    return best;
}

double MonteCarloImpl::playout(Position& pos) {
    Color currentToMove = pos.side_to_move();
    size_t ply          = 0;

    std::array<StateInfo, MAX_PLY + kMaxPlayoutDepth + 4> states{};

    while (ply < kMaxPlayoutDepth)
    {
        if (pos.is_draw(int(ply)))
            return 0.5;

        MoveList<LEGAL> moves(pos);
        if (moves.size() == 0)
        {
            if (pos.checkers())
                return currentToMove == rootColor ? 0.0 : 1.0;
            return 0.5;
        }

        std::uniform_int_distribution<size_t> dist(0, moves.size() - 1);
        auto                                  it = moves.begin();
        std::advance(it, dist(rng()));
        const Move move = *it;
        pos.do_move(move, states[ply]);
        currentToMove = pos.side_to_move();
        ++ply;
    }

    int eval = Eval::simple_eval(pos);
    if (pos.side_to_move() != rootColor)
        eval = -eval;

    return logistic_from_eval(eval);
}

}  // namespace

namespace MCTS {

bool should_use_mcts(const Position& pos,
                     const Config&   cfg,
                     bool            maybeDraw,
                     int             legalMoveCount,
                     Color           us) {
    if (maybeDraw || legalMoveCount <= 1)
        return false;

    PositionMetrics metrics = collect_metrics(pos, us);

    if (is_petrosian_high(metrics) || is_petrosian_middle(metrics))
        return true;

    if (!cfg.explore)
        return false;

    return is_tal_high(metrics) || is_capablanca(metrics);
}

std::optional<Result> search(const Position& root,
                             const Config&   cfg,
                             std::atomic_bool& stopFlag,
                             Color           perspective) {
    MonteCarloImpl impl(root, perspective, cfg, stopFlag);
    return impl.run();
}

}  // namespace MCTS

}  // namespace Revolution
