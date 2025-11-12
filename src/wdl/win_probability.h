#pragma once

#include <cstdint>
#include <string>

#include "../position.h"
#include "../types.h"

namespace Stockfish {
namespace WDLModel {

struct WDL {
    std::uint8_t win;
    std::uint8_t draw;
    std::uint8_t loss;
};

void init();
bool is_initialized();
WDL  get_wdl_by_material(Value value, int materialClamp);
WDL  get_wdl(Value value, const Position& pos);
std::uint8_t get_win_probability_by_material(Value value, int materialClamp);
std::uint8_t get_win_probability(Value value, const Position& pos);
std::uint8_t get_win_probability(Value value, int plies);
std::string  wdl(Value v, const Position& pos);

struct WinRateParams {
    double a;
    double b;
};

WinRateParams win_rate_params(const Position& pos);
WinRateParams win_rate_params(int materialClamp);
int           win_rate_model(Value v, const Position& pos);

}  // namespace WDLModel
}  // namespace Stockfish

