#ifndef ENGINE_CONFIG_H_INCLUDED
#define ENGINE_CONFIG_H_INCLUDED

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#define ENGINE_STRINGIFY_DETAIL(x) #x
#define ENGINE_STRINGIFY(x) ENGINE_STRINGIFY_DETAIL(x)

#ifndef ENGINE_NAME
#    define ENGINE_NAME revolution-cluster-mpi-121025
#endif

#ifndef ENGINE_BUILD_DATE
#    define ENGINE_BUILD_DATE
#endif

namespace Stockfish {
namespace EngineConfig {
namespace detail {

inline std::string sanitize_macro_string(std::string_view raw) {
    // Remove matching surrounding quotes repeatedly (handles nested quoting styles)
    while (raw.size() >= 2
           && ((raw.front() == '"' && raw.back() == '"') || (raw.front() == '\'' && raw.back() == '\'')))
        raw = raw.substr(1, raw.size() - 2);

    std::string result(raw);
    std::string cleaned;
    cleaned.reserve(result.size());

    for (std::size_t i = 0; i < result.size(); ++i)
    {
        if (result[i] == '\\' && i + 1 < result.size())
        {
            char next = result[i + 1];
            if (next == '"' || next == '\\' || next == '\'')
            {
                cleaned.push_back(next);
                ++i;
                continue;
            }
        }

        cleaned.push_back(result[i]);
    }

    if (cleaned.size() >= 2
        && ((cleaned.front() == '"' && cleaned.back() == '"')
            || (cleaned.front() == '\'' && cleaned.back() == '\'')))
    {
        cleaned.erase(cleaned.begin());
        cleaned.pop_back();
    }

    // Trim surrounding whitespace that may have come from macro expansion
    auto trim = [](std::string& str) {
        auto not_space = [](unsigned char ch) { return !std::isspace(static_cast<int>(ch)); };

        auto begin_it = std::find_if(str.begin(), str.end(), not_space);
        auto end_it   = std::find_if(str.rbegin(), str.rend(), not_space).base();

        if (begin_it >= end_it)
        {
            str.clear();
            return;
        }

        str.assign(begin_it, end_it);
    };

    trim(cleaned);
    return cleaned;
}

}  // namespace detail

inline const std::string& name() {
    static const std::string value = detail::sanitize_macro_string(ENGINE_STRINGIFY(ENGINE_NAME));
    return value;
}

inline const std::string& build_date() {
    static const std::string value = detail::sanitize_macro_string(ENGINE_STRINGIFY(ENGINE_BUILD_DATE));
    return value;
}

}  // namespace EngineConfig

inline const std::string& engine_name_string() { return EngineConfig::name(); }

inline const std::string& engine_build_date_string() { return EngineConfig::build_date(); }

}  // namespace Stockfish

#endif  // ENGINE_CONFIG_H_INCLUDED
