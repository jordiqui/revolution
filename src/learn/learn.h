#ifndef LEARN_LEARN_H_INCLUDED
#define LEARN_LEARN_H_INCLUDED

#include <unordered_map>
#include <vector>

#include "types.h"
#include "ucioption.h"
#include "position.h"

namespace Stockfish {

enum class LearningMode {
    Off,
    Standard,
    Self,
};

struct LearningMove {
    Depth depth       = 0;
    Value score       = VALUE_NONE;
    Move  move        = Move::none();
    int   performance = 100;
};

struct PersistedLearningMove {
    Key          key{};
    LearningMove learningMove;
};

struct QLearningMove {
    PersistedLearningMove persistedLearningMove;
    int                   materialClamp = 0;
};

class LearningData {
    bool         isPaused;
    bool         isReadOnly;
    bool         needPersisting;
    LearningMode learningMode;

    std::unordered_multimap<Key, LearningMove*> HT;
    std::vector<void*>                           mainDataBuffers;
    std::vector<void*>                           newMovesDataBuffers;

    bool load(const std::string& filename);
    void insert_or_update(PersistedLearningMove* plm, bool qLearning);

   public:
    LearningData();
    ~LearningData();

    void               pause();
    void               resume();
    [[nodiscard]] bool is_paused() const { return isPaused; }

    void               quick_reset_exp();
    void               set_learning_mode(OptionsMap& options, const std::string& mode);
    [[nodiscard]] LearningMode learning_mode() const;
    [[nodiscard]] bool         is_enabled() const { return learningMode != LearningMode::Off; }

    void               set_readonly(bool ro) { isReadOnly = ro; }
    [[nodiscard]] bool is_readonly() const { return isReadOnly; }

    void clear();
    void init(OptionsMap& options);
    void persist(const OptionsMap& options);

    void add_new_learning(Key key, const LearningMove& lm);

    int                        probeByMaxDepthAndScore(Key key, const LearningMove*& learningMove);
    const LearningMove*        probe_move(Key key, Move move);
    std::vector<LearningMove*> probe(Key key);
    static void                sortLearningMoves(std::vector<LearningMove*>& learningMoves);
    static void                show_exp(const Position& pos);
};

extern LearningData LD;

}  // namespace Stockfish

#endif
