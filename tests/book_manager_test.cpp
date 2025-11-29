#include <cassert>
#include <deque>
#include <memory>
#include <string>

#include "../src/book/book_manager.h"
#include "../src/book/book.h"
#include "../src/book/book_utils.h"
#include "../src/movegen.h"
#include "../src/position.h"
#include "../src/ucioption.h"
#include "../src/uci.h"
#include "../src/search.h"
#include "../src/tt.h"

using namespace Stockfish;

namespace Stockfish {
struct TTEntry {};

// Minimal stubs to avoid linking the full UCI and tablebase stacks in tests.
std::string UCIEngine::square(Square) { return ""; }
std::string UCIEngine::move(Move, bool) { return ""; }
std::string UCIEngine::format_score(const Score&) { return ""; }
std::string UCIEngine::wdl(Value, const Position&) { return ""; }
int         UCIEngine::to_cp(Value, const Position&) { return 0; }
std::string UCIEngine::to_lower(std::string str) { return str; }
Move        UCIEngine::to_move(const Position&, std::string) { return Move::none(); }
Search::LimitsType UCIEngine::parse_limits(std::istream&) { return Search::LimitsType(); }

namespace Tablebases {
int         MaxCardinality = 0;
WDLScore    probe_wdl(Position&, ProbeState*) { return WDLScore(); }
int         probe_dtz(Position&, ProbeState*) { return 0; }
void        init(const Option&) {}
}
}  // namespace Stockfish

static Stockfish::TTEntry g_tt_stub;
Stockfish::TTEntry* Stockfish::TranspositionTable::first_entry(const Key) const { return &g_tt_stub; }

class StubBook : public Book::Book {
   public:
    StubBook(std::string t, Move m, Stockfish::Book::LoadStats stats = {}) :
        bookType(std::move(t)),
        move(m) {
        if (stats.validMoves == 0 && move != Move::none())
            stats.validMoves = 1;

        loadStats = stats;
    }

    std::string type() const override { return bookType; }
    bool        open(const std::string&) override { return true; }
    void        close() override {}

    Move probe(const Position&, size_t, bool) const override { return move; }
    void show_moves(const Position&) const override {}

    Stockfish::Book::LoadStats load_stats() const override { return loadStats; }

   private:
    std::string               bookType;
    Move                      move;
    Stockfish::Book::LoadStats loadStats;
};

int main() {
    Bitboards::init();
    Position::init();

    OptionsMap options;
    options.add(Stockfish::Book::format_option_key("CTG/BIN Book %d File", 1), Option("binbook.bin"));
    options.add(Stockfish::Book::format_option_key("Book %d Width", 1), Option(1, 1, 100));
    options.add(Stockfish::Book::format_option_key("Book %d Depth", 1), Option(255, 1, 255));
    options.add(Stockfish::Book::format_option_key("(CTG) Book %d Only Green", 1), Option(false));

    options.add(Stockfish::Book::format_option_key("CTG/BIN Book %d File", 2), Option("ctgbook.ctg"));
    options.add(Stockfish::Book::format_option_key("Book %d Width", 2), Option(1, 1, 100));
    options.add(Stockfish::Book::format_option_key("Book %d Depth", 2), Option(255, 1, 255));
    options.add(Stockfish::Book::format_option_key("(CTG) Book %d Only Green", 2), Option(false));

    BookManager manager;

    Move binMove = Move(SQ_E2, SQ_E4);
    Move ctgMove = Move(SQ_D2, SQ_D4);

    manager.set_book_for_testing(0, new StubBook("BIN", binMove));
    manager.set_book_for_testing(1, new StubBook("CTG", ctgMove));

    StateListPtr states(new std::deque<StateInfo>(1));
    Position     pos;
    pos.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", false, &states->back());

    Move result = manager.probe(pos, options);
    assert(result == binMove);

    manager.set_book_for_testing(0, nullptr);
    result = manager.probe(pos, options);
    assert(result == ctgMove);

    manager.set_book_for_testing(1, nullptr);
    result = manager.probe(pos, options);
    assert(result == Move::none());

    return 0;
}
