#ifndef CLUSTER_H_INCLUDED
#define CLUSTER_H_INCLUDED

#include <string>
#include <vector>

namespace Stockfish {

class Engine;
namespace Search {
struct LimitsType;
}

namespace Cluster {

void init(int& argc, char**& argv);
void finalize();
void signal_quit();
bool active();
bool is_master();
int  size();

#ifdef USE_MPI

enum class Command : int { Quit = 0, Go = 1 };

struct LimitsMessage {
    long long time[2];
    long long inc[2];
    long long npmsec;
    long long movetime;
    long long startTime;
    unsigned long long nodes;
    int movestogo;
    int depth;
    int mate;
    int perft;
    int infinite;
    int ponderMode;
};

struct ResultMessage {
    int hasMove;
    int scoreType;
    int scoreValue;
    int extra;
    int depth;
    int selDepth;
    unsigned long long nodes;
};

LimitsMessage pack_limits(const Search::LimitsType& limits);
Search::LimitsType unpack_limits(const LimitsMessage& message);

void broadcast_command(Command cmd);
Command receive_command();
void send_limits(int dest, const LimitsMessage& message);
LimitsMessage recv_limits(int src);
void send_fen(int dest, const std::string& fen);
std::string recv_fen(int src);
void send_moves(int dest, const std::vector<std::string>& moves);
std::vector<std::string> recv_moves(int src);
void send_result(int dest, const ResultMessage& result, const std::string& bestmove, const std::string& ponder);
void recv_result(int src, ResultMessage& result, std::string& bestmove, std::string& ponder);

void worker_loop();

#else  // !USE_MPI

inline void worker_loop() {}

#endif

}  // namespace Cluster

}  // namespace Stockfish

#endif  // CLUSTER_H_INCLUDED

