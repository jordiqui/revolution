#ifndef ZOBRIST_H_INCLUDED
#define ZOBRIST_H_INCLUDED

#include "types.h"

namespace Stockfish::Zobrist {

extern Key psq[PIECE_NB][SQUARE_NB];
extern Key enpassant[FILE_NB];
extern Key castling[CASTLING_RIGHT_NB];
extern Key side;
extern Key noPawns;

void init();

}  // namespace Stockfish::Zobrist

#endif  // ZOBRIST_H_INCLUDED
