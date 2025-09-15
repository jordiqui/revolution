#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "bitboard.h"
#include "evaluate.h"
#include "nnue/nnue_accumulator.h"
#include "nnue/network.h"
#include "position.h"
#include "types.h"

using namespace Stockfish;

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

    std::stringstream            placementStream(placement);
    std::vector<std::string>     ranks;
    std::string                  rankToken;

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

namespace {

Value evaluate_fen(const std::string& fen, const Eval::NNUE::Networks& networks) {
    StateInfo st;
    Position  pos;

    pos.set(fen, false, &st);

    if (pos.checkers())
        throw std::runtime_error("Eval::evaluate requires the side to move not to be in check.");

    Eval::NNUE::AccumulatorStack  accumulators;
    Eval::NNUE::AccumulatorCaches caches(networks);

    return Eval::evaluate(networks, pos, accumulators, caches, 0);
}

std::string gather_fen_from_args(int argc, char* argv[]) {
    std::string fen;
    for (int i = 1; i < argc; ++i)
    {
        if (!fen.empty())
            fen += ' ';
        fen += argv[i];
    }

    if (!fen.empty())
        return fen;

    std::string line;
    if (std::getline(std::cin, line) && !line.empty())
        return line;

    return "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
}

Eval::NNUE::Networks load_networks() {
    using namespace Eval::NNUE;

    NetworkBig   big({std::string(Eval::EvalFileDefaultNameBig), "None", ""}, EmbeddedNNUEType::BIG);
    NetworkSmall small({std::string(Eval::EvalFileDefaultNameSmall), "None", ""}, EmbeddedNNUEType::SMALL);

    const std::string bigName   = std::string(Eval::EvalFileDefaultNameBig);
    const std::string smallName = std::string(Eval::EvalFileDefaultNameSmall);

    auto ensure_exists = [](const std::string& path) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("Unable to open NNUE file: " + path);
    };

    ensure_exists(bigName);
    ensure_exists(smallName);

    big.load("", bigName);
    small.load("", smallName);

    return Networks(std::move(big), std::move(small));
}

}  // namespace

int main(int argc, char* argv[]) {
    try
    {
        Bitboards::init();
        Position::init();

        Eval::NNUE::Networks networks = load_networks();

        const std::string fen     = gather_fen_from_args(argc, argv);
        const std::string mirrored = mirror_fen(fen);

        const Value originalEval = evaluate_fen(fen, networks);
        const Value mirrorEval   = evaluate_fen(mirrored, networks);

        std::cout << "Original FEN:  " << fen << '\n';
        std::cout << "Mirrored FEN:  " << mirrored << '\n';
        std::cout << "Eval(original): " << int(originalEval) << '\n';
        std::cout << "Eval(mirror):   " << int(mirrorEval) << '\n';

        if (originalEval != -mirrorEval)
        {
            std::cerr << "WARNING: Eval(FEN) != -Eval(MirrorFEN). Sum = "
                      << int(originalEval) + int(mirrorEval) << '\n';
        }

        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}

