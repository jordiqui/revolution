#include "learn/learn.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>

#include "misc.h"
#include "uci.h"
#include "wdl/win_probability.h"

namespace Stockfish {

namespace {

std::string resolve_experience_path(const std::string& filename) {
    if (filename.empty())
        return filename;

    std::filesystem::path path(filename);
    if (path.is_absolute() || path.has_parent_path())
        return path.lexically_normal().string();

    auto base = CommandLine::get_working_directory();
    if (base.empty())
        return path.lexically_normal().string();

    std::filesystem::path combined = std::filesystem::path(base) / path;
    return combined.lexically_normal().string();
}

LearningMode identify_learning_mode(const std::string& mode) {
    if (mode == "Off")
        return LearningMode::Off;

    if (mode == "Self")
        return LearningMode::Self;

    return LearningMode::Standard;
}

bool should_update(const LearningMove& existing_move, const LearningMove& learning_move) {
    if (learning_move.depth > existing_move.depth)
        return true;

    if (learning_move.depth < existing_move.depth)
        return false;

    if (learning_move.score != existing_move.score)
        return true;

    return learning_move.performance != existing_move.performance;
}

}  // namespace

LearningData LD;

LearningData::LearningData() :
    isPaused(false),
    isReadOnly(false),
    needPersisting(false),
    learningMode(LearningMode::Standard) {}

LearningData::~LearningData() { clear(); }

bool LearningData::load(const std::string& filename) {
    std::ifstream in(resolve_experience_path(filename), std::ios::in | std::ios::binary);

    if (!in.is_open())
        return false;

    in.seekg(0, std::ios::end);
    const auto fileSize = static_cast<std::size_t>(in.tellg());

    if (fileSize % sizeof(PersistedLearningMove))
    {
        std::cerr << "info string The file <" << filename << "> with size <" << fileSize
                  << "> is not a valid experience file" << std::endl;
        return false;
    }

    void* fileData = std::malloc(fileSize);
    if (!fileData)
    {
        std::cerr << "info string Failed to allocate <" << fileSize
                  << "> bytes to read experience file <" << filename << ">" << std::endl;
        return false;
    }

    in.seekg(0, std::ios::beg);
    in.read(static_cast<char*>(fileData), static_cast<std::streamsize>(fileSize));
    if (!in)
    {
        std::free(fileData);

        std::cerr << "info string Failed to read <" << fileSize << "> bytes from experience file <"
                  << filename << ">" << std::endl;
        return false;
    }

    in.close();

    mainDataBuffers.push_back(fileData);

    const bool qLearning = learningMode == LearningMode::Self;
    auto*      persistedLearningMove = static_cast<PersistedLearningMove*>(fileData);

    do
    {
        insert_or_update(persistedLearningMove, qLearning);
        ++persistedLearningMove;
    } while (reinterpret_cast<std::size_t>(persistedLearningMove)
             < reinterpret_cast<std::size_t>(fileData) + fileSize);

    return true;
}

void LearningData::insert_or_update(PersistedLearningMove* plm, bool qLearning) {
    const auto [first, second] = HT.equal_range(plm->key);

    if (first == second)
    {
        HT.insert({plm->key, &plm->learningMove});
        needPersisting = true;
        return;
    }

    const auto itr = std::find_if(first, second, [&](const auto& entry) {
        return entry.second->move == plm->learningMove.move;
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
    if (bestNewMoveCandidate == currentBestMove)
        return;

    bool newBestMove = false;

    if (qLearning)
    {
        if (bestNewMoveCandidate->score > currentBestMove->score)
            newBestMove = true;
    }
    else if ((currentBestMove->depth < bestNewMoveCandidate->depth)
             || (currentBestMove->depth == bestNewMoveCandidate->depth
                 && currentBestMove->score <= bestNewMoveCandidate->score))
    {
        newBestMove = true;
    }

    if (newBestMove)
    {
        static LearningMove swapBuffer;

        swapBuffer              = *bestNewMoveCandidate;
        *bestNewMoveCandidate   = *currentBestMove;
        *currentBestMove        = swapBuffer;
        needPersisting          = true;
    }
}

void LearningData::clear() {
    HT.clear();

    for (void* buffer : mainDataBuffers)
        std::free(buffer);
    mainDataBuffers.clear();

    for (void* buffer : newMovesDataBuffers)
        std::free(buffer);
    newMovesDataBuffers.clear();
}

void LearningData::init(OptionsMap& options) {
    clear();

    const bool selfLearning = int(options["Self Q-learning"]);
    learningMode = identify_learning_mode(selfLearning ? "Self" : "Standard");

    load("experience.exp");

    std::vector<std::string> slaveFiles;

    if (load("experience_new.exp"))
        slaveFiles.push_back(resolve_experience_path("experience_new.exp"));

    int index = 0;
    while (true)
    {
        auto filename = "experience" + std::to_string(index) + ".exp";
        if (!load(filename))
            break;

        slaveFiles.push_back(resolve_experience_path(filename));
        ++index;
    }

    if (!slaveFiles.empty())
        persist(options);

    for (const auto& file : slaveFiles)
        std::remove(file.c_str());

    needPersisting = false;
}

void LearningData::quick_reset_exp() {
    std::cout << "Loading experience file: experience.exp" << std::endl;

    const auto path = resolve_experience_path("experience.exp");
    std::ifstream file(path, std::ifstream::binary | std::ifstream::ate);
    if (!file)
    {
        std::cerr << "Failed to load experience file" << std::endl;
        return;
    }

    const auto file_size = file.tellg();
    constexpr std::streamsize entry_size = sizeof(PersistedLearningMove);
    const auto total_entries             = file_size / entry_size;

    file.close();

    std::cout << "Total entries in the file: " << total_entries << std::endl;

    if (!load("experience.exp"))
    {
        std::cerr << "Failed to load experience file" << std::endl;
        return;
    }

    std::cout << "Successfully loaded experience file" << std::endl;

    std::size_t entry_count = 0;
    for (auto& [key, learning_move] : HT)
    {
        ++entry_count;

        const auto new_performance = WDLModel::get_win_probability(learning_move->score, learning_move->depth);

        std::cout << "Updating entry " << entry_count << "/" << total_entries << " Key " << key
                  << " Value " << learning_move->score << " Depth " << learning_move->depth
                  << ": old performance=" << static_cast<int>(learning_move->performance)
                  << ", new performance=" << static_cast<int>(new_performance) << std::endl;

        learning_move->performance = new_performance;
    }

    needPersisting = true;
    std::cout << "Finished updating performances. Total processed entries: " << entry_count << std::endl;
}

void LearningData::set_learning_mode(OptionsMap& options, const std::string& mode) {
    LearningMode newMode = identify_learning_mode(mode);
    if (newMode == learningMode)
        return;

    learningMode = newMode;
    init(options);
}

LearningMode LearningData::learning_mode() const { return learningMode; }

void LearningData::persist(const OptionsMap& options) {
    if (HT.empty() || !needPersisting)
        return;

    if (isReadOnly)
    {
        assert(false);
        return;
    }

    std::string experienceFilename;
    std::string tempExperienceFilename;

    if (int(options["Concurrent Experience"]))
    {
        static std::string uniqueStr;

        if (uniqueStr.empty())
        {
            PRNG         prng(now());
            std::stringstream ss;
            ss << std::hex << prng.rand<std::uint64_t>();
            uniqueStr = ss.str();
        }

        experienceFilename     = resolve_experience_path("experience-" + uniqueStr + ".exp");
        tempExperienceFilename = resolve_experience_path("experience_new-" + uniqueStr + ".exp");
    }
    else
    {
        experienceFilename     = resolve_experience_path("experience.exp");
        tempExperienceFilename = resolve_experience_path("experience_new.exp");
    }

    std::ofstream outputFile(tempExperienceFilename, std::ofstream::trunc | std::ofstream::binary);
    PersistedLearningMove persistedLearningMove;
    for (auto& [key, move] : HT)
    {
        persistedLearningMove.key          = key;
        persistedLearningMove.learningMove = *move;
        if (persistedLearningMove.learningMove.depth != 0)
            outputFile.write(reinterpret_cast<char*>(&persistedLearningMove), sizeof(persistedLearningMove));
    }
    outputFile.close();

    std::remove(experienceFilename.c_str());
    std::rename(tempExperienceFilename.c_str(), experienceFilename.c_str());

    needPersisting = false;
}

void LearningData::pause() { isPaused = true; }

void LearningData::resume() { isPaused = false; }

void LearningData::add_new_learning(Key key, const LearningMove& lm) {
    auto* newPlm = static_cast<PersistedLearningMove*>(std::malloc(sizeof(PersistedLearningMove)));
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
    int                 maxScore     = -1;

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

        if (move->depth > maxDepth)
        {
            maxDepth     = move->depth;
            maxScore     = move->score;
            maxDepthMove = move;
        }
        else if (move->depth == maxDepth && move->score > maxScore)
        {
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

    const auto itr = std::find_if(range.first, range.second, [&](const auto& entry) {
        return entry.second->move == move;
    });

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

        if (a->performance != b->performance)
            return a->performance > b->performance;

        return a->score > b->score;
    });
}

void LearningData::show_exp(const Position& pos) {
    sync_cout << pos << std::endl;
    std::cout << "Experience: ";
    auto learningMoves = LD.probe(pos.key());
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
