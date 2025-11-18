#include "learn.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>

#include "../misc.h"
#include "../uci.h"
#include "../wdl/win_probability.h"

namespace Stockfish {

LearningData LD;

namespace {

LearningMode identify_learning_mode(const std::string& lm) {
    if (lm == "Off")
        return LearningMode::Off;

    if (lm == "Standard")
        return LearningMode::Standard;

    return LearningMode::Self;
}

}  // namespace

LearningData::LearningData() :
    isPaused(false),
    isReadOnly(false),
    needPersisting(false),
    learningMode(LearningMode::Standard) {}

LearningData::~LearningData() { clear(); }

void LearningData::set_storage_directory(std::string path) {
    if (path.empty())
    {
        storageRoot.clear();
        return;
    }

    try
    {
        storageRoot = std::filesystem::path(std::move(path));
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        storageRoot.clear();
        std::cerr << "info string Failed to set experience storage directory: " << e.what()
                  << std::endl;
    }
}

std::filesystem::path LearningData::resolve_path(const std::string& filename) const {
    std::filesystem::path path(filename);

    if (path.empty())
        return path;

    if (path.is_absolute() || storageRoot.empty())
        return path;

    return storageRoot / path;
}

bool LearningData::load(const std::filesystem::path& filename) {
    if (filename.empty())
        return false;

    std::ifstream in(filename, std::ios::in | std::ios::binary);

    if (!in.is_open())
        return false;

    in.seekg(0, std::ios::end);
    const std::streamoff fileSize = in.tellg();

    if (fileSize <= 0 || fileSize % sizeof(PersistedLearningMove) != 0)
    {
        std::cerr << "info string The file <" << filename.string() << "> with size <" << fileSize
                  << "> is not a valid experience file" << std::endl;
        return false;
    }

    void* fileData = std::malloc(static_cast<size_t>(fileSize));
    if (!fileData)
    {
        std::cerr << "info string Failed to allocate <" << fileSize
                  << "> bytes to read experience file <" << filename.string() << ">"
                  << std::endl;
        return false;
    }

    in.seekg(0, std::ios::beg);
    in.read(static_cast<char*>(fileData), fileSize);
    if (!in)
    {
        std::free(fileData);

        std::cerr << "info string Failed to read <" << fileSize
                  << "> bytes from experience file <" << filename.string() << ">" << std::endl;
        return false;
    }

    in.close();

    mainDataBuffers.push_back(fileData);

    const bool qLearning = learningMode == LearningMode::Self;
    auto*      persisted = static_cast<PersistedLearningMove*>(fileData);
    auto*      end       = reinterpret_cast<PersistedLearningMove*>(
      reinterpret_cast<std::uintptr_t>(fileData) + static_cast<std::uintptr_t>(fileSize));

    for (; persisted < end; ++persisted)
        insert_or_update(persisted, qLearning);

    return true;
}

inline bool should_update(const LearningMove existing_move, const LearningMove learning_move) {
    if (learning_move.depth > existing_move.depth)
        return true;

    if (learning_move.depth < existing_move.depth)
        return false;

    if (learning_move.score != existing_move.score)
        return true;

    return learning_move.performance != existing_move.performance;
}

void LearningData::insert_or_update(PersistedLearningMove* plm, bool qLearning) {
    const auto [first, second] = HT.equal_range(plm->key);

    if (first == second)
    {
        HT.insert({plm->key, &plm->learningMove});
        needPersisting = true;
        return;
    }

    const auto itr = std::find_if(first, second, [&plm](const auto& p) {
        return p.second->move == plm->learningMove.move;
    });

    LearningMove* bestNewMoveCandidate = nullptr;
    if (itr == second)
    {
        HT.insert({plm->key, &plm->learningMove});
        bestNewMoveCandidate = &plm->learningMove;
        needPersisting       = true;
    }
    else
    {
        LearningMove* existingMove = itr->second;
        if (should_update(*existingMove, plm->learningMove))
        {
            *existingMove        = plm->learningMove;
            bestNewMoveCandidate = existingMove;
            needPersisting       = true;
        }
    }

    if (!bestNewMoveCandidate)
        return;

    LearningMove* currentBestMove = first->second;
    bool          newBestMove     = false;

    if (bestNewMoveCandidate != currentBestMove)
    {
        if (qLearning)
        {
            if (bestNewMoveCandidate->score > currentBestMove->score)
                newBestMove = true;
        }
        else if (currentBestMove->depth < bestNewMoveCandidate->depth
                 || (currentBestMove->depth == bestNewMoveCandidate->depth
                     && currentBestMove->score <= bestNewMoveCandidate->score))
            newBestMove = true;
    }

    if (!newBestMove)
        return;

    static LearningMove tmp;
    tmp                    = *bestNewMoveCandidate;
    *bestNewMoveCandidate  = *currentBestMove;
    *currentBestMove       = tmp;
    needPersisting         = true;
}

void LearningData::clear() {
    HT.clear();

    for (void* p : mainDataBuffers)
        std::free(p);
    mainDataBuffers.clear();

    for (void* p : newMovesDataBuffers)
        std::free(p);
    newMovesDataBuffers.clear();
}

void LearningData::init(OptionsMap& o) {
    OptionsMap& options = o;

    clear();
    learningMode = identify_learning_mode(options["Self Q-learning"] ? "Self" : "Standard");

    load(resolve_path("experience.exp"));

    std::vector<std::filesystem::path> auxiliaryFiles;

    const auto pendingPath = resolve_path("experience_new.exp");
    if (load(pendingPath))
        auxiliaryFiles.push_back(pendingPath);

    for (int i = 0;; ++i)
    {
        const auto candidate = resolve_path("experience" + std::to_string(i) + ".exp");
        std::error_code candidateExistsEc;
        if (!std::filesystem::exists(candidate, candidateExistsEc))
            break;

        if (candidateExistsEc)
        {
            std::cerr << "info string Cannot check experience file <" << candidate.string()
                      << ">: " << candidateExistsEc.message() << std::endl;
            break;
        }

        if (load(candidate))
            auxiliaryFiles.push_back(candidate);
    }

    if (!auxiliaryFiles.empty())
        persist(options);

    for (const auto& path : auxiliaryFiles)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    needPersisting = false;
}

void LearningData::quick_reset_exp() {
    const auto experiencePath = resolve_path("experience.exp");
    std::cout << "Loading experience file: " << experiencePath.string() << std::endl;

    std::ifstream file(experiencePath, std::ifstream::binary | std::ifstream::ate);
    if (!file)
    {
        std::cerr << "Failed to load experience file" << std::endl;
        return;
    }

    const std::streamsize file_size     = file.tellg();
    constexpr std::streamsize entry_size = sizeof(PersistedLearningMove);
    const std::streamsize total_entries  = file_size / entry_size;

    file.close();

    std::cout << "Total entries in the file: " << total_entries << std::endl;

    if (!load(experiencePath))
    {
        std::cerr << "Failed to load experience file" << std::endl;
        return;
    }

    std::cout << "Successfully loaded experience file" << std::endl;

    int entry_count = 0;
    for (auto& [key, learning_move] : HT)
    {
        (void) key;
        ++entry_count;

        const auto old_performance = learning_move->performance;
        const auto new_performance = WDLModel::get_win_probability(learning_move->score,
                                                                   learning_move->depth);

        std::cout << "Updating entry " << entry_count << "/" << total_entries << ": old performance="
                  << static_cast<int>(old_performance)
                  << ", new performance=" << static_cast<int>(new_performance) << std::endl;

        learning_move->performance = new_performance;
    }

    needPersisting = true;
    std::cout << "Finished updating performances. Total processed entries: " << entry_count
              << std::endl;
}

void LearningData::set_learning_mode(OptionsMap& options, const std::string& lm) {
    LearningMode newLearningMode = identify_learning_mode(lm);
    if (newLearningMode == learningMode)
        return;

    init(options);
}

LearningMode LearningData::learning_mode() const { return learningMode; }

void LearningData::persist(const OptionsMap& options) {
    (void) options;
    if (HT.empty() || !needPersisting)
        return;

    if (isReadOnly)
    {
        assert(false);
        return;
    }

    const auto experienceFilename     = resolve_path("experience.exp");
    const auto tempExperienceFilename = resolve_path("experience_new.exp");

    if (!experienceFilename.parent_path().empty())
    {
        std::error_code mkdirEc;
        std::filesystem::create_directories(experienceFilename.parent_path(), mkdirEc);

        if (mkdirEc)
        {
            std::cerr << "info string Failed to create directory for experience file <"
                      << experienceFilename.parent_path().string()
                      << ">: " << mkdirEc.message() << std::endl;
            return;
        }
    }
    if (!tempExperienceFilename.parent_path().empty())
    {
        std::error_code mkdirEc;
        std::filesystem::create_directories(tempExperienceFilename.parent_path(), mkdirEc);

        if (mkdirEc)
        {
            std::cerr << "info string Failed to create directory for experience file <"
                      << tempExperienceFilename.parent_path().string()
                      << ">: " << mkdirEc.message() << std::endl;
            return;
        }
    }

    std::ofstream outputFile(tempExperienceFilename, std::ofstream::trunc | std::ofstream::binary);
    if (!outputFile)
    {
        std::cerr << "info string Failed to open temporary experience file: "
                  << tempExperienceFilename.string() << std::endl;
        return;
    }

    PersistedLearningMove persisted{};
    for (auto& kvp : HT)
    {
        persisted.key          = kvp.first;
        persisted.learningMove = *kvp.second;
        if (persisted.learningMove.depth > Depth(0))
            outputFile.write(reinterpret_cast<const char*>(&persisted), sizeof(persisted));
    }
    outputFile.close();

    std::error_code removeEc;
    std::filesystem::remove(experienceFilename, removeEc);

    std::error_code renameEc;
    std::filesystem::rename(tempExperienceFilename, experienceFilename, renameEc);
    if (renameEc)
        std::cerr << "info string Failed to rename temporary experience file: "
                  << tempExperienceFilename.string() << " -> " << experienceFilename.string()
                  << std::endl;

    needPersisting = false;
}

void LearningData::pause() { isPaused = true; }

void LearningData::resume() { isPaused = false; }

void LearningData::add_new_learning(Key key, const LearningMove& lm) {
    auto* newPlm = static_cast<PersistedLearningMove*>(
      std::malloc(sizeof(PersistedLearningMove)));
    if (!newPlm)
    {
        std::cerr << "info string Failed to allocate <" << sizeof(PersistedLearningMove)
                  << "> bytes for new learning entry" << std::endl;
        return;
    }

    newMovesDataBuffers.push_back(newPlm);

    newPlm->key          = key;
    newPlm->learningMove = lm;

    insert_or_update(newPlm, learningMode == LearningMode::Self);
}

int LearningData::probeByMaxDepthAndScore(Key key, const LearningMove*& learningMove) {
    const LearningMove* maxDepthMove = nullptr;
    int                 maxDepth     = -1;
    int                 maxScore     = -VALUE_INFINITE;

    auto range = HT.equal_range(key);
    if (range.first == range.second)
    {
        learningMove = nullptr;
        return 0;
    }

    int siblings = 0;
    for (auto it = range.first; it != range.second; ++it)
    {
        ++siblings;
        LearningMove* move = it->second;
        if (move->depth > maxDepth || (move->depth == maxDepth && move->score > maxScore))
        {
            maxDepth     = move->depth;
            maxScore     = move->score;
            maxDepthMove = move;
        }
    }

    learningMove = maxDepthMove;
    return siblings;
}

const LearningMove* LearningData::probe_move(Key key, Move move) {
    auto range = HT.equal_range(key);

    if (range.first == range.second)
        return nullptr;

    const auto itr =
      std::find_if(range.first, range.second, [&move](const auto& p) { return p.second->move == move; });

    if (itr == range.second)
        return nullptr;

    return itr->second;
}

std::vector<LearningMove*> LearningData::probe(Key key) {
    std::vector<LearningMove*> result;
    auto                       range = HT.equal_range(key);
    for (auto it = range.first; it != range.second; ++it)
        result.push_back(it->second);

    return result;
}

void LearningData::sortLearningMoves(std::vector<LearningMove*>& learningMoves) {
    std::sort(learningMoves.begin(), learningMoves.end(), [](const LearningMove* a, const LearningMove* b) {
        if (a->depth != b->depth)
            return a->depth > b->depth;

        const int winProbA = a->performance;
        const int winProbB = b->performance;

        if (winProbA != winProbB)
            return winProbA > winProbB;

        return a->score > b->score;
    });
}

void LearningData::show_exp(const Position& pos) {
    sync_cout << pos << std::endl;
    std::cout << "Experience: ";
    std::vector<LearningMove*> learningMoves = LD.probe(pos.key());
    if (learningMoves.empty())
    {
        std::cout << "No experience data found for this position" << sync_endl;
        return;
    }

    sortLearningMoves(learningMoves);

    std::cout << std::endl;
    for (const auto& move : learningMoves)
    {
        const int winProb = move->performance;
        std::cout << "move: " << UCIEngine::move(move->move, pos.is_chess960())
                  << " depth: " << move->depth << " value: " << move->score
                  << " win probability: " << winProb << std::endl;
    }
    std::cout << sync_endl;
}

}  // namespace Stockfish

