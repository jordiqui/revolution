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

#include "zobrist.h"

#include <algorithm>

#include "misc.h"

namespace Stockfish {

Key Zobrist::psq[PIECE_NB][SQUARE_NB];
Key Zobrist::enpassant[FILE_NB];
Key Zobrist::castling[CASTLING_RIGHT_NB];
Key Zobrist::side;
Key Zobrist::noPawns;

namespace {

constexpr int zobristSeed = 1070372;

}  // namespace

void Zobrist::init() {

    PRNG rng(zobristSeed);

    for (Piece pc = W_PAWN; pc <= B_KING; pc = Piece(pc + 1))
        for (Square s = SQ_A1; s <= SQ_H8; s = Square(s + 1))
            psq[pc][s] = rng.rand<Key>();

    std::fill_n(psq[W_PAWN] + SQ_A8, 8, 0);
    std::fill_n(psq[B_PAWN], 8, 0);

    for (File f = FILE_A; f <= FILE_H; f = File(f + 1))
        enpassant[f] = rng.rand<Key>();

    for (int cr = NO_CASTLING; cr <= ANY_CASTLING; ++cr)
        castling[cr] = rng.rand<Key>();

    side    = rng.rand<Key>();
    noPawns = rng.rand<Key>();
}

}  // namespace Stockfish
