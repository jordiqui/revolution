#include "wdl/win_probability.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace Stockfish {
namespace WDLModel {

namespace {

constexpr std::size_t ValueRange    = 8001;   // Values from -4000 to 4000 inclusive
constexpr std::size_t MaterialRange = 62;     // Material clamp from 17 to 78 inclusive
constexpr std::size_t TableSize     = ValueRange * MaterialRange;

WDL wdl_data[TableSize];

std::size_t index(Value value, int materialClamp) {
    const int valueClamp    = std::clamp(static_cast<int>(value), -4000, 4000);
    const int materialIndex = std::clamp(materialClamp, 17, 78);
    return static_cast<std::size_t>(valueClamp + 4000) * MaterialRange + (materialIndex - 17);
}

bool initialized = false;

}  // namespace

void init() {
    if (initialized)
        return;

    initialized = true;

    for (int valueClamp = -4000; valueClamp <= 4000; ++valueClamp)
    {
        for (int materialClamp = 17; materialClamp <= 78; ++materialClamp)
        {
            auto [a, b] = win_rate_params(materialClamp);

            double w = 0.5 + 1000 / (1 + std::exp((a - double(valueClamp)) / b));
            double l = 0.5 + 1000 / (1 + std::exp((a - double(-valueClamp)) / b));
            double d = 1000 - w - l;

            wdl_data[index(valueClamp, materialClamp)] = {static_cast<std::uint8_t>(std::round(w / 10.0)),
                                                          static_cast<std::uint8_t>(std::round(d / 10.0)),
                                                          static_cast<std::uint8_t>(std::round(l / 10.0))};
        }
    }
}

bool is_initialized() { return initialized; }

WDL get_precomputed_wdl(int valueClamp, int materialClamp) {
    return wdl_data[index(valueClamp, materialClamp)];
}

WDL get_wdl_by_material(Value value, int materialClamp) {
    const int valueClamp = std::clamp(static_cast<int>(value), -4000, 4000);
    return get_precomputed_wdl(valueClamp, materialClamp);
}

WDL get_wdl(Value value, const Position& pos) {
    const int material = pos.count<PAWN>() + 3 * pos.count<KNIGHT>() + 3 * pos.count<BISHOP>()
                       + 5 * pos.count<ROOK>() + 9 * pos.count<QUEEN>();
    const int materialClamp = std::clamp(material, 17, 78);
    return get_wdl_by_material(value, materialClamp);
}

std::uint8_t get_win_probability_by_material(Value value, int materialClamp) {
    const WDL wdl = get_wdl_by_material(value, materialClamp);
    return static_cast<std::uint8_t>(wdl.win + wdl.draw / 2);
}

std::uint8_t get_win_probability(Value value, const Position& pos) {
    const WDL wdl = get_wdl(value, pos);
    return static_cast<std::uint8_t>(wdl.win + wdl.draw / 2);
}

std::uint8_t get_win_probability(Value value, int plies) {
    const int full_moves = plies / 2 + 1;
    auto [a, b]          = win_rate_params(full_moves);

    double w = 0.5 + 1000 / (1 + std::exp((a - double(value)) / b));
    double l = 0.5 + 1000 / (1 + std::exp((a - double(-value)) / b));
    double d = 1000 - w - l;

    return static_cast<std::uint8_t>(std::round((w + d / 2.0) / 10.0));
}

std::string wdl(Value value, const Position& pos) {
    const WDL wdl = get_wdl(value, pos);
    std::stringstream ss;
    ss << int(wdl.win * 10) << " " << int(wdl.draw * 10) << " " << int(wdl.loss * 10);
    return ss.str();
}

WinRateParams win_rate_params(int materialClamp) {
    double m = std::clamp(materialClamp, 17, 78) / 58.0;
    constexpr double as[] = {-13.50030198, 40.92780883, -36.82753545, 386.83004070};
    constexpr double bs[] = {96.53354896, -165.79058388, 90.89679019, 49.29561889};

    double a = (((as[0] * m + as[1]) * m + as[2]) * m) + as[3];
    double b = (((bs[0] * m + bs[1]) * m + bs[2]) * m) + bs[3];

    return {a, b};
}

WinRateParams win_rate_params(const Position& pos) {
    int material = pos.count<PAWN>() + 3 * pos.count<KNIGHT>() + 3 * pos.count<BISHOP>()
                 + 5 * pos.count<ROOK>() + 9 * pos.count<QUEEN>();

    double m = std::clamp(material, 17, 78) / 58.0;

    constexpr double as[] = {-37.45051876, 121.19101539, -132.78783573, 420.70576692};
    constexpr double bs[] = {90.26261072, -137.26549898, 71.10130540, 51.35259597};

    double a = (((as[0] * m + as[1]) * m + as[2]) * m) + as[3];
    double b = (((bs[0] * m + bs[1]) * m + bs[2]) * m) + bs[3];

    return {a, b};
}

int win_rate_model(Value value, const Position& pos) {
    auto [a, b] = win_rate_params(pos);
    return int(0.5 + 1000 / (1 + std::exp((a - double(value)) / b)));
}

}  // namespace WDLModel
}  // namespace Stockfish
