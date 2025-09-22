#include <algorithm>
#include <array>
#include <cctype>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "bitboard.h"
#include "history.h"
#include "movegen.h"
#include "position.h"
#include "syzygy/tbprobe.h"
#include "tt.h"
#include "uci.h"

using namespace Stockfish;

namespace Stockfish {

template<>
Move* generate<EVASIONS>(const Position&, Move* moveList) {
    return moveList;
}

template<>
Move* generate<NON_EVASIONS>(const Position&, Move* moveList) {
    return moveList;
}

template<>
Move* generate<LEGAL>(const Position&, Move* moveList) {
    return moveList;
}

TTEntry* TranspositionTable::first_entry(const Key) const {
    return nullptr;
}

std::string UCIEngine::square(Square s) {
    return std::string{char('a' + file_of(s)), char('1' + rank_of(s))};
}

std::string UCIEngine::move(Move m, bool chess960) {
    (void) chess960;
    return square(m.from_sq()) + square(m.to_sq());
}

namespace Tablebases {

int MaxCardinality = 0;

WDLScore probe_wdl(Position&, ProbeState* result) {
    if (result)
        *result = FAIL;
    return WDLDraw;
}

int probe_dtz(Position&, ProbeState* result) {
    if (result)
        *result = FAIL;
    return 0;
}

void init(const std::string&, bool) {}

void release() {}

}  // namespace Tablebases

}  // namespace Stockfish

namespace {

std::string mirror_fen(const std::string& fen) {
    std::istringstream fenStream(fen);
    std::string        placement;
    std::string        active;
    std::string        castling;
    std::string        enPassant;
    std::string        halfmoveClock;
    std::string        fullmoveNumber;

    if (!(fenStream >> placement >> active >> castling >> enPassant >> halfmoveClock >> fullmoveNumber))
        throw std::invalid_argument("Invalid FEN string: " + fen);

    std::stringstream        placementStream(placement);
    std::vector<std::string> ranks;
    std::string              rankToken;

    while (std::getline(placementStream, rankToken, '/'))
    {
        if (rankToken.empty())
            throw std::invalid_argument("Invalid FEN rank in: " + fen);
        ranks.push_back(rankToken);
    }

    if (ranks.size() != 8)
        throw std::invalid_argument("Invalid FEN rank count in: " + fen);

    std::reverse(ranks.begin(), ranks.end());

    std::string mirroredPlacement;
    for (std::size_t i = 0; i < ranks.size(); ++i)
    {
        if (i)
            mirroredPlacement += '/';
        mirroredPlacement += ranks[i];
    }

    auto toggle_case = [](char c) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::islower(uc))
            return static_cast<char>(std::toupper(uc));
        if (std::isupper(uc))
            return static_cast<char>(std::tolower(uc));
        return c;
    };

    std::transform(mirroredPlacement.begin(), mirroredPlacement.end(), mirroredPlacement.begin(), toggle_case);

    std::string mirroredActive = active;
    if (mirroredActive == "w")
        mirroredActive = "b";
    else if (mirroredActive == "b")
        mirroredActive = "w";
    else
        throw std::invalid_argument("Invalid FEN active color in: " + fen);

    std::string mirroredCastling = castling;
    if (mirroredCastling != "-")
        std::transform(mirroredCastling.begin(), mirroredCastling.end(), mirroredCastling.begin(), toggle_case);

    std::string mirroredEnPassant = enPassant;
    if (mirroredEnPassant != "-")
    {
        if (mirroredEnPassant.size() != 2 || mirroredEnPassant[0] < 'a' || mirroredEnPassant[0] > 'h')
            throw std::invalid_argument("Invalid FEN en passant square in: " + fen);

        if (mirroredEnPassant[1] == '3')
            mirroredEnPassant[1] = '6';
        else if (mirroredEnPassant[1] == '6')
            mirroredEnPassant[1] = '3';
        else
            throw std::invalid_argument("Unexpected en passant rank in: " + fen);
    }

    std::string remainder;
    std::getline(fenStream, remainder);

    std::ostringstream result;
    result << mirroredPlacement << ' ' << mirroredActive << ' ' << mirroredCastling << ' '
           << mirroredEnPassant << ' ' << halfmoveClock << ' ' << fullmoveNumber;
    if (!remainder.empty())
        result << remainder;

    return result.str();
}

bool run_symmetry_check() {
    CorrectionHistory<NonPawn> history;
    history.fill(0);

    const std::string baseFen = "4k3/2q5/8/3N4/8/8/8/4K3 w - - 0 1";

    std::string mirroredFen;
    try
    {
        mirroredFen = mirror_fen(baseFen);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Failed to mirror FEN: " << ex.what() << '\n';
        return false;
    }

    StateInfo stBase;
    StateInfo stMirror;
    Position  posBase;
    Position  posMirror;

    posBase.set(baseFen, false, &stBase);
    posMirror.set(mirroredFen, false, &stMirror);

    auto apply = [&](const Position& pos, int delta) {
        const Color mover    = pos.side_to_move();
        const int   whiteIdx = non_pawn_index<WHITE>(pos);
        const int   blackIdx = non_pawn_index<BLACK>(pos);

        history[whiteIdx][WHITE][mover] << correction_sign(WHITE, mover) * delta;
        history[blackIdx][BLACK][mover] << correction_sign(BLACK, mover) * delta;
    };

    const std::array<int, 3> deltas = {120, -64, 48};
    for (int delta : deltas)
    {
        apply(posBase, delta);
        apply(posMirror, -delta);
    }

    const int baseWhiteIndex   = non_pawn_index<WHITE>(posBase);
    const int baseBlackIndex   = non_pawn_index<BLACK>(posBase);
    const int mirrorWhiteIndex = non_pawn_index<WHITE>(posMirror);
    const int mirrorBlackIndex = non_pawn_index<BLACK>(posMirror);

    const int baseWhiteValue   = history[baseWhiteIndex][WHITE][WHITE];
    const int mirrorBlackValue = history[mirrorBlackIndex][BLACK][BLACK];
    const int baseBlackValue   = history[baseBlackIndex][BLACK][WHITE];
    const int mirrorWhiteValue = history[mirrorWhiteIndex][WHITE][BLACK];

    if (baseWhiteValue != -mirrorBlackValue || baseBlackValue != -mirrorWhiteValue)
    {
        std::cerr << "History values not symmetric. "
                  << "baseWhite=" << baseWhiteValue << " (index=" << baseWhiteIndex << ")"
                  << ", mirrorBlack=" << mirrorBlackValue << " (index=" << mirrorBlackIndex << ")"
                  << ", baseBlack=" << baseBlackValue << " (index=" << baseBlackIndex << ")"
                  << ", mirrorWhite=" << mirrorWhiteValue << " (index=" << mirrorWhiteIndex << ")"
                  << '\n';
        return false;
    }

    return true;
}

}  // namespace

int main() {
    Bitboards::init();
    Position::init();

    return run_symmetry_check() ? 0 : 1;
}
