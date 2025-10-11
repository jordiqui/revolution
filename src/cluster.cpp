#include "cluster.h"

#include "engine.h"
#include "movegen.h"
#include "search.h"
#include "tune.h"
#include "uci.h"

#include <atomic>
#include <cstdint>
#include <string_view>

namespace Stockfish {
namespace Cluster {

namespace {
#ifdef USE_MPI
#include <mpi.h>

constexpr int TAG_LIMITS        = 1;
constexpr int TAG_FEN           = 2;
constexpr int TAG_MOVES         = 3;
constexpr int TAG_RESULT_DATA   = 4;
constexpr int TAG_RESULT_BEST   = 5;
constexpr int TAG_RESULT_PONDER = 6;

std::atomic<bool> mpiInitialized = false;
int               worldRank       = 0;
int               worldSize       = 1;
bool              clusterActive   = false;

template<typename... Ts>
struct overload: Ts... {
    using Ts::operator()...;
};
template<typename... Ts>
overload(Ts...) -> overload<Ts...>;

#endif
}  // namespace

void init(int& argc, char**& argv) {
#ifdef USE_MPI
    if (!mpiInitialized.load())
    {
        MPI_Init(&argc, &argv);
        mpiInitialized = true;
        MPI_Comm_rank(MPI_COMM_WORLD, &worldRank);
        MPI_Comm_size(MPI_COMM_WORLD, &worldSize);
        clusterActive = worldSize > 1;
    }
#else
    (void) argc;
    (void) argv;
#endif
}

void finalize() {
#ifdef USE_MPI
    if (mpiInitialized.load())
    {
        MPI_Finalize();
        mpiInitialized = false;
        worldRank       = 0;
        worldSize       = 1;
        clusterActive   = false;
    }
#endif
}

void signal_quit() {
#ifdef USE_MPI
    if (clusterActive && worldRank == 0)
        broadcast_command(Command::Quit);
#endif
}

bool active() {
#ifdef USE_MPI
    return clusterActive;
#else
    return false;
#endif
}

bool is_master() {
#ifdef USE_MPI
    return worldRank == 0;
#else
    return true;
#endif
}

int size() {
#ifdef USE_MPI
    return worldSize;
#else
    return 1;
#endif
}

#ifdef USE_MPI

Cluster::LimitsMessage pack_limits(const Search::LimitsType& limits) {
    LimitsMessage msg{};
    msg.time[WHITE] = limits.time[WHITE];
    msg.time[BLACK] = limits.time[BLACK];
    msg.inc[WHITE]  = limits.inc[WHITE];
    msg.inc[BLACK]  = limits.inc[BLACK];
    msg.npmsec      = limits.npmsec;
    msg.movetime    = limits.movetime;
    msg.startTime   = limits.startTime;
    msg.nodes       = limits.nodes;
    msg.movestogo   = limits.movestogo;
    msg.depth       = limits.depth;
    msg.mate        = limits.mate;
    msg.perft       = limits.perft;
    msg.infinite    = limits.infinite;
    msg.ponderMode  = limits.ponderMode ? 1 : 0;
    return msg;
}

Search::LimitsType unpack_limits(const LimitsMessage& message) {
    Search::LimitsType limits;
    limits.time[WHITE] = message.time[WHITE];
    limits.time[BLACK] = message.time[BLACK];
    limits.inc[WHITE]  = message.inc[WHITE];
    limits.inc[BLACK]  = message.inc[BLACK];
    limits.npmsec      = message.npmsec;
    limits.movetime    = message.movetime;
    limits.startTime   = message.startTime;
    limits.nodes       = message.nodes;
    limits.movestogo   = message.movestogo;
    limits.depth       = message.depth;
    limits.mate        = message.mate;
    limits.perft       = message.perft;
    limits.infinite    = message.infinite;
    limits.ponderMode  = message.ponderMode != 0;
    return limits;
}

void broadcast_command(Command cmd) {
    int value = static_cast<int>(cmd);
    MPI_Bcast(&value, 1, MPI_INT, 0, MPI_COMM_WORLD);
}

Command receive_command() {
    int value = 0;
    MPI_Bcast(&value, 1, MPI_INT, 0, MPI_COMM_WORLD);
    return static_cast<Command>(value);
}

void send_limits(int dest, const LimitsMessage& message) {
    MPI_Send(&message, sizeof(message), MPI_BYTE, dest, TAG_LIMITS, MPI_COMM_WORLD);
}

LimitsMessage recv_limits(int src) {
    LimitsMessage msg{};
    MPI_Recv(&msg, sizeof(msg), MPI_BYTE, src, TAG_LIMITS, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    return msg;
}

void send_string_impl(int dest, const std::string& value, int tag) {
    int length = static_cast<int>(value.size());
    MPI_Send(&length, 1, MPI_INT, dest, tag, MPI_COMM_WORLD);
    if (length > 0)
        MPI_Send(value.data(), length, MPI_CHAR, dest, tag, MPI_COMM_WORLD);
}

std::string recv_string_impl(int src, int tag) {
    int length = 0;
    MPI_Recv(&length, 1, MPI_INT, src, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    std::string value(length, '\0');
    if (length > 0)
        MPI_Recv(value.data(), length, MPI_CHAR, src, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    return value;
}

void send_string_vector_impl(int dest, const std::vector<std::string>& values, int tag) {
    int count = static_cast<int>(values.size());
    MPI_Send(&count, 1, MPI_INT, dest, tag, MPI_COMM_WORLD);
    for (const auto& v : values)
        send_string_impl(dest, v, tag);
}

std::vector<std::string> recv_string_vector_impl(int src, int tag) {
    int count = 0;
    MPI_Recv(&count, 1, MPI_INT, src, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    std::vector<std::string> values;
    values.reserve(count);
    for (int i = 0; i < count; ++i)
        values.push_back(recv_string_impl(src, tag));
    return values;
}

void send_fen(int dest, const std::string& fen) {
    send_string_impl(dest, fen, TAG_FEN);
}

std::string recv_fen(int src) {
    return recv_string_impl(src, TAG_FEN);
}

void send_moves(int dest, const std::vector<std::string>& moves) {
    send_string_vector_impl(dest, moves, TAG_MOVES);
}

std::vector<std::string> recv_moves(int src) {
    return recv_string_vector_impl(src, TAG_MOVES);
}

void send_result(int dest, const ResultMessage& result, const std::string& bestmove, const std::string& ponder) {
    MPI_Send(&result, sizeof(result), MPI_BYTE, dest, TAG_RESULT_DATA, MPI_COMM_WORLD);
    send_string_impl(dest, bestmove, TAG_RESULT_BEST);
    send_string_impl(dest, ponder, TAG_RESULT_PONDER);
}

void recv_result(int src, ResultMessage& result, std::string& bestmove, std::string& ponder) {
    MPI_Recv(&result, sizeof(result), MPI_BYTE, src, TAG_RESULT_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    bestmove = recv_string_impl(src, TAG_RESULT_BEST);
    ponder   = recv_string_impl(src, TAG_RESULT_PONDER);
}

namespace {

struct WorkerCapture {
    Score score;
    bool  hasScore   = false;
    int   depth      = 0;
    int   selDepth   = 0;
    int   nodesValid = 0;
    uint64_t nodes   = 0;
    std::string bestmove;
    std::string ponder;
    bool        hasMove = false;
};

ResultMessage make_result(const WorkerCapture& capture) {
    ResultMessage result{};
    result.hasMove = capture.hasMove ? 1 : 0;
    result.depth   = capture.depth;
    result.selDepth = capture.selDepth;
    result.nodes    = capture.nodes;
    if (capture.hasScore)
    {
        capture.score.visit(overload{
          [&](Score::Mate mate) {
              result.scoreType  = 2;
              result.scoreValue = mate.plies;
              result.extra      = 0;
          },
          [&](Score::Tablebase tb) {
              result.scoreType  = 3;
              result.scoreValue = tb.plies;
              result.extra      = tb.win ? 1 : 0;
          },
          [&](Score::InternalUnits units) {
              result.scoreType  = 1;
              result.scoreValue = units.value;
              result.extra      = 0;
          }});
    }
    else
    {
        result.scoreType  = 0;
        result.scoreValue = 0;
        result.extra      = 0;
    }
    return result;
}

}  // namespace

void worker_loop() {
    if (!clusterActive)
        return;

    Engine workerEngine;
    Tune::init(workerEngine.get_options());

    bool running = true;

    while (running)
    {
        Command command = receive_command();
        switch (command)
        {
        case Command::Quit:
            running = false;
            break;
        case Command::Go:
        {
            auto limitsMessage = recv_limits(0);
            auto limits        = unpack_limits(limitsMessage);
            std::string fen    = recv_fen(0);
            auto        moves  = recv_moves(0);

            workerEngine.set_position(fen, {});

            WorkerCapture capture;

            workerEngine.set_on_update_full([&](const Engine::InfoFull& info) {
                capture.score    = info.score;
                capture.hasScore = true;
                capture.depth    = info.depth;
                capture.selDepth = info.selDepth;
                capture.nodes    = info.nodes;
            });
            workerEngine.set_on_update_no_moves([&](const Engine::InfoShort& info) {
                capture.score    = info.score;
                capture.hasScore = true;
            });
            workerEngine.set_on_iter([](const auto&) {});
            workerEngine.set_on_bestmove([&](std::string_view best, std::string_view ponder) {
                capture.bestmove = std::string(best);
                capture.ponder   = std::string(ponder);
                capture.hasMove  = true;
            });
            workerEngine.set_on_verify_networks([](const auto&) {});

            if (!limits.perft && !moves.empty())
            {
                limits.searchmoves = moves;
                workerEngine.go(limits);
                workerEngine.wait_for_search_finished();
            }
            else if (limits.perft)
            {
                // Leave as not handled; cluster perft not supported
            }

            ResultMessage result = make_result(capture);
            send_result(0, result, capture.bestmove, capture.ponder);
            break;
        }
        }
    }
}

#endif  // USE_MPI

}  // namespace Cluster
}  // namespace Stockfish

#ifndef USE_MPI

namespace Stockfish {
namespace Cluster {

void init(int& argc, char**& argv) {
    (void) argc;
    (void) argv;
}

void finalize() {}

void signal_quit() {}

bool active() {
    return false;
}

bool is_master() {
    return true;
}

int size() {
    return 1;
}

}  // namespace Cluster
}  // namespace Stockfish

#endif

