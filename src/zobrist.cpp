#include "zobrist.h"

#include <algorithm>

#include "misc.h"

namespace Stockfish::Zobrist {

Key psq[PIECE_NB][SQUARE_NB];
Key enpassant[FILE_NB];
Key castling[CASTLING_RIGHT_NB];
Key side;
Key noPawns;

void init() {
    PRNG rng(1070372);

    for (int pc = W_PAWN; pc <= B_KING; ++pc)
        for (int s = SQ_A1; s <= SQ_H8; ++s)
            psq[pc][s] = rng.rand<Key>();

    std::fill_n(psq[W_PAWN] + SQ_A8, 8, Key(0));
    std::fill_n(psq[B_PAWN] + SQ_A1, 8, Key(0));

    for (int f = FILE_A; f <= FILE_H; ++f)
        enpassant[f] = rng.rand<Key>();

    for (int cr = NO_CASTLING; cr <= ANY_CASTLING; ++cr)
        castling[cr] = rng.rand<Key>();

    side    = rng.rand<Key>();
    noPawns = rng.rand<Key>();
}

}  // namespace Stockfish::Zobrist
