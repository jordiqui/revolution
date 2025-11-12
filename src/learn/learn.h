#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "../position.h"
#include "../types.h"
#include "../ucioption.h"

namespace Stockfish {

enum class LearningMode {
    Off,
    Standard,
    Self,
};

struct LearningMove {
    Depth depth       = Depth(0);
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
   public:
    LearningData();
    ~LearningData();

    void               set_storage_directory(std::string path);
    void               pause();
    void               resume();
    [[nodiscard]] bool is_paused() const { return isPaused; }

    void quick_reset_exp();
    void set_learning_mode(OptionsMap& options, const std::string& mode);
    [[nodiscard]] LearningMode learning_mode() const;
    [[nodiscard]] bool         is_enabled() const { return learningMode != LearningMode::Off; }

    void               set_readonly(bool ro) { isReadOnly = ro; }
    [[nodiscard]] bool is_readonly() const { return isReadOnly; }

    void clear();
    void init(OptionsMap& options);
    void persist(const OptionsMap& options);

    void add_new_learning(Key key, const LearningMove& lm);

    int                       probeByMaxDepthAndScore(Key key, const LearningMove*& learningMove);
    const LearningMove*       probe_move(Key key, Move move);
    std::vector<LearningMove*> probe(Key key);
    static void               sortLearningMoves(std::vector<LearningMove*>& learningMoves);
    static void               show_exp(const Position& pos);

   private:
    bool                        load(const std::filesystem::path& filename);
    void                        insert_or_update(PersistedLearningMove* plm, bool qLearning);
    [[nodiscard]] std::filesystem::path resolve_path(const std::string& filename) const;

    std::filesystem::path                        storageRoot;
    bool                                         isPaused;
    bool                                         isReadOnly;
    bool                                         needPersisting;
    LearningMode                                 learningMode;
    std::unordered_multimap<Key, LearningMove*>  HT;
    std::vector<void*>                           mainDataBuffers;
    std::vector<void*>                           newMovesDataBuffers;
};

extern LearningData LD;

}  // namespace Stockfish

