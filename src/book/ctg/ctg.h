#ifndef CTG_BOOK_H_INCLUDED
#define CTG_BOOK_H_INCLUDED

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "book/book.h"
#include "book/file_mapping.h"

namespace Stockfish::Book::CTG {

enum class CtgMoveAnnotation {
    None               = 0x00,
    GoodMove           = 0x01,
    BadMove            = 0x02,
    ExcellentMove      = 0x03,
    LosingMove         = 0x04,
    InterestingMove    = 0x05,
    DubiousMove        = 0x06,
    OnlyMove           = 0x08,
    Zugzwang           = 0x16,
    Unknown            = 0xFF,
};

enum class CtgMoveRecommendation {
    NoPreference = 0x00,
    RedMove      = 0x40,
    GreenMove    = 0x80,
    Unknown      = 0xFF,
};

enum class CtgMoveCommentary {
    None                  = 0x00,
    Equal                 = 0x0B,
    Unclear               = 0x0D,
    EqualPlus             = 0x0E,
    PlusEqual             = 0x0F,
    MinusSlashPlus        = 0x10,
    PlusSlashMinus        = 0x11,
    PlusMinus             = 0x13,
    DevelopmentAdvantage  = 0x20,
    Initiative            = 0x24,
    WithAttack            = 0x28,
    Compensation          = 0x2C,
    Counterplay           = 0x84,
    Zeitnot               = 0x8A,
    Novelty               = 0x92,
    Unknown               = 0xFF,
};

struct CtgMoveStats {
    int32_t win       = 0;
    int32_t loss      = 0;
    int32_t draw      = 0;
    int32_t ratingDiv = 0;
    int32_t ratingSum = 0;
};

struct CtgMove : CtgMoveStats {
   private:
    Move pseudoMove = Move::none();
    Move sfMove     = Move::none();

   public:
    CtgMoveAnnotation     annotation     = CtgMoveAnnotation::Unknown;
    CtgMoveRecommendation recommendation = CtgMoveRecommendation::Unknown;
    CtgMoveCommentary     commentary     = CtgMoveCommentary::Unknown;

    int64_t moveWeight = std::numeric_limits<int64_t>::min();

    void set_from_to(const Position& pos, Square from, Square to) {
        PieceType promotionPiece = NO_PIECE_TYPE;

        if (from == SQ_E1 && to == SQ_G1 && pos.piece_on(from) == W_KING && pos.piece_on(SQ_H1) == W_ROOK
            && pos.can_castle(WHITE_OO))
            to = SQ_H1;
        else if (from == SQ_E8 && to == SQ_G8 && pos.piece_on(from) == B_KING && pos.piece_on(SQ_H8) == B_ROOK
                 && pos.can_castle(BLACK_OO))
            to = SQ_H8;
        else if (from == SQ_E1 && to == SQ_C1 && pos.piece_on(from) == W_KING && pos.piece_on(SQ_A1) == W_ROOK
                 && pos.can_castle(WHITE_OOO))
            to = SQ_A1;
        else if (from == SQ_E8 && to == SQ_C8 && pos.piece_on(from) == B_KING && pos.piece_on(SQ_A8) == B_ROOK
                 && pos.can_castle(BLACK_OOO))
            to = SQ_A8;
        else if (((rank_of(from) == RANK_7 && rank_of(to) == RANK_8)
                  || (rank_of(from) == RANK_2 && rank_of(to) == RANK_1))
                 && type_of(pos.piece_on(from)) == PAWN)
            promotionPiece = QUEEN;

        pseudoMove = promotionPiece == NO_PIECE_TYPE ? Move(from, to)
                                                     : Move::make<PROMOTION>(from, to, promotionPiece);
    }

    Move pseudo_move() const {
        assert(pseudoMove != Move::none());
        return pseudoMove;
    }

    Move set_sf_move(Move m) { return sfMove = m; }

    Move sf_move() const {
        assert(sfMove != Move::none());
        return sfMove;
    }

    int64_t weight() const {
        assert(moveWeight != std::numeric_limits<int64_t>::min());
        return moveWeight;
    }

    bool green() const {
        return (int(recommendation) & int(CtgMoveRecommendation::GreenMove))
               && annotation != CtgMoveAnnotation::BadMove && annotation != CtgMoveAnnotation::LosingMove
               && annotation != CtgMoveAnnotation::InterestingMove && annotation != CtgMoveAnnotation::DubiousMove;
    }

    bool red() const { return (int(recommendation) & int(CtgMoveRecommendation::RedMove)); }
};

struct CtgMoveList : public std::vector<CtgMove> {
    CtgMoveStats positionStats{};

    void calculate_weights() {
        if (this->empty())
            return;

        auto calculate_pseudo_weight = [](CtgMove& m, int win, int loss, int draw) -> int64_t {
            static constexpr int64_t MAX_WEIGHT = std::numeric_limits<int16_t>::max();
            static constexpr int64_t MIN_WEIGHT = std::numeric_limits<int16_t>::min();

            int64_t              winFactor  = 2;
            int64_t              lossFactor = 2;
            constexpr int64_t    drawFactor = 1;

            winFactor += m.green() ? 10 : 0;
            lossFactor += m.red() ? 10 : 0;

            switch (m.annotation)
            {
            case CtgMoveAnnotation::GoodMove:
                winFactor += m.green() ? 5 : 0;
                break;

            case CtgMoveAnnotation::BadMove:
                lossFactor += 5;
                break;

            case CtgMoveAnnotation::ExcellentMove:
                winFactor += m.green() ? 10 : 0;
                break;

            case CtgMoveAnnotation::LosingMove:
                lossFactor += 10;
                break;

            case CtgMoveAnnotation::InterestingMove:
                winFactor += 2;
                break;

            case CtgMoveAnnotation::DubiousMove:
                lossFactor += 2;
                break;

            case CtgMoveAnnotation::Zugzwang:
                ++winFactor;
                ++lossFactor;
                break;

            case CtgMoveAnnotation::OnlyMove:
                winFactor += m.green() ? MAX_WEIGHT : 0;
                break;

            default:
                break;
            }

            if (winFactor == MAX_WEIGHT)
                return MAX_WEIGHT;

            if (lossFactor == MAX_WEIGHT)
                return MIN_WEIGHT;

            return ((win + 100) * winFactor - (loss + 100) * lossFactor + (draw + 100) * drawFactor);
        };

        int64_t avgGames = 0;
        for (const CtgMove& m : *this)
            avgGames += m.win + m.loss + m.draw;

        avgGames /= static_cast<int64_t>(this->size());
        if (avgGames == 0)
            avgGames = 300;

        int64_t maxWeight = std::numeric_limits<int64_t>::min();
        int64_t minWeight = std::numeric_limits<int64_t>::max();
        for (CtgMove& m : *this)
        {
            const int64_t games = m.win + m.loss + m.draw;
            const int64_t diff  = (avgGames - games) / 3;

            const int64_t win  = std::max<int64_t>(m.win + diff, 0);
            const int64_t loss = std::max<int64_t>(m.loss + diff, 0);
            const int64_t draw = std::max<int64_t>(m.draw + diff, 0);

            assert(win + draw + loss >= 0);
            if (win + loss + draw == 0)
                m.moveWeight = 0;
            else
                m.moveWeight = calculate_pseudo_weight(m, static_cast<int>(win), static_cast<int>(loss), static_cast<int>(draw));

            minWeight = std::min(minWeight, m.moveWeight);
            maxWeight = std::max(maxWeight, m.moveWeight);
        }

        for (CtgMove& m : *this)
        {
            if (maxWeight == minWeight)
            {
                m.moveWeight = 0;
                continue;
            }

            m.moveWeight = (m.moveWeight - minWeight) * 200 / (maxWeight - minWeight) - 100;
        }

        std::stable_sort(this->begin(), this->end(), [](const CtgMove& lhs, const CtgMove& rhs) {
            return lhs.weight() > rhs.weight();
        });
    }
};

struct CtgPositionData {
    Square        epSquare       = SQ_NONE;
    bool          invert         = false;
    bool          flip           = false;
    char          board[64]      = {};
    unsigned char encodedPosition[32]{};
    int32_t       encodedPosLen  = 0;
    int32_t       encodedBitsLeft = 0;
    unsigned char positionPage[256]{};

    CtgPositionData() = default;
    CtgPositionData(const CtgPositionData&)            = delete;
    CtgPositionData& operator=(const CtgPositionData&) = delete;
};

class CtgBook: public Book {
   public:
    CtgBook();
    ~CtgBook() override;

    CtgBook(const CtgBook&)            = delete;
    CtgBook& operator=(const CtgBook&) = delete;

    std::string type() const override;

    bool open(const std::string& f) override;
    void close() override;

    bool is_open() const;

    Move probe(const Position& pos, size_t width, bool onlyGreen) const override;

    void show_moves(const Position& pos) const override;

   private:
    bool decode(const Position& pos, CtgPositionData& positionData) const;
    void decode_board(const Position& pos, CtgPositionData& positionData) const;
    void invert_board(CtgPositionData& positionData) const;
    bool needs_flipping(const Position& pos) const;
    void flip_board(const Position& pos, CtgPositionData& positionData) const;

    void     encode_position(const Position& pos, CtgPositionData& positionData) const;
    bool     read_position_data(CtgPositionData& positionData, uint32_t pageNum) const;
    uint32_t gen_position_hash(CtgPositionData& positionData) const;
    bool     lookup_position(CtgPositionData& positionData) const;

    void get_stats(const CtgPositionData& positionData, CtgMoveStats& stats, bool isMove) const;
    Move get_pseudo_move(const CtgPositionData& positionData, int moveNum) const;
    bool get_move(const Position& pos, const CtgPositionData& positionData, int moveNum, CtgMove& ctgMove) const;
    void get_moves(const Position& pos, const CtgPositionData& positionData, CtgMoveList& ctgMoveList) const;

    ::Stockfish::Book::FileMapping cto;
    ::Stockfish::Book::FileMapping ctg;
    uint32_t          pageLowerBound;
    uint32_t          pageUpperBound;
    bool              isOpen;
};

}  // namespace Stockfish::Book::CTG

#endif
