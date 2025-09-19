#include <cstdlib>
#include <iomanip>
#include <iostream>

#define private public
#include "polybook.h"
#undef private

#include "bitboard.h"
#include "position.h"

int main() {
    using namespace Stockfish;

    Bitboards::init();
    Position::init();

    PolyBook book;

    StateInfo whiteState;
    Position  whitePosition;
    whitePosition.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", false, &whiteState);

    constexpr Key expectedStartKey = 0x463B96181691FC9CULL;
    const Key     whiteKey         = book.polyglot_key(whitePosition);

    if (whiteKey != expectedStartKey)
    {
        std::cerr << "Unexpected PolyGlot key for start position. Expected 0x"
                  << std::hex << std::uppercase << expectedStartKey << ", got 0x" << whiteKey
                  << std::dec << std::nouppercase << std::endl;
        return EXIT_FAILURE;
    }

    StateInfo blackState;
    Position  blackPosition;
    blackPosition.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1", false, &blackState);

    constexpr Key polyglotTurn = 0xF8D626AAAF278509ULL;
    const Key     blackKey     = book.polyglot_key(blackPosition);

    if ((whiteKey ^ polyglotTurn) != blackKey)
    {
        std::cerr << "PolyGlot key did not toggle by turn constant when switching side to move." << std::endl;
        return EXIT_FAILURE;
    }

    if ((whiteKey ^ blackKey) != polyglotTurn)
    {
        std::cerr << "PolyGlot turn constant mismatch after toggling side to move." << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
