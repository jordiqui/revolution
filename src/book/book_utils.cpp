#include "book_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "../misc.h"

namespace Stockfish::Book {

namespace {

std::string& base_directory_storage() {
    static std::string directory;
    return directory;
}

std::string unquote(std::string s) {
    if (s.size() > 1 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

std::filesystem::path make_path(const std::string& path) {
    std::string cleaned = unquote(path);
    std::replace(cleaned.begin(), cleaned.end(), '\\', std::filesystem::path::preferred_separator);
    std::filesystem::path p(cleaned);

    if (p.is_relative())
    {
        const auto& base = base_directory_storage();
        if (!base.empty())
        {
            std::filesystem::path basePath(base);
            p = basePath / p;
        }
    }

    return p.lexically_normal();
}

}  // namespace

void set_base_directory(std::string directory) {
    base_directory_storage() = std::move(directory);
}

const std::string& base_directory() { return base_directory_storage(); }

std::string format_option_key(const char* pattern, int index) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), pattern, index);
    return std::string(buffer);
}

bool is_empty_filename(const std::string& filename) {
    if (filename.empty())
        return true;

    auto it = std::find_if_not(filename.begin(), filename.end(), [](char c) {
        return std::isspace(static_cast<unsigned char>(c));
    });

    if (it == filename.end())
        return true;

    std::string_view trimmed(&*it, filename.end() - it);
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
        trimmed.remove_suffix(1);

    if (trimmed.empty())
        return true;

    std::string lower(trimmed.begin(), trimmed.end());
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    return lower == "<empty>";
}

std::string map_path(const std::string& path) {
    if (is_empty_filename(path))
        return {};

    std::error_code ec;
    auto           mapped = make_path(path);
    auto           abs    = std::filesystem::absolute(mapped, ec);
    if (!ec)
        mapped = abs;

    return mapped.string();
}

bool is_same_file(const std::string& lhs, const std::string& rhs) {
    std::error_code ec;
    const auto      p1 = make_path(lhs);
    const auto      p2 = make_path(rhs);

    if (p1 == p2)
        return true;

    const auto canon1 = std::filesystem::weakly_canonical(p1, ec);
    if (ec)
        return false;

    const auto canon2 = std::filesystem::weakly_canonical(p2, ec);
    if (ec)
        return false;

    return canon1 == canon2;
}

std::size_t get_file_size(const std::string& path) {
    if (is_empty_filename(path))
        return static_cast<std::size_t>(-1);

    std::error_code ec;
    auto            size = std::filesystem::file_size(make_path(path), ec);
    if (ec)
        return static_cast<std::size_t>(-1);

    return static_cast<std::size_t>(size);
}

std::string format_bytes(std::uint64_t bytes, int decimals) {
    static const std::uint64_t KB = 1024;
    static const std::uint64_t MB = KB * 1024;
    static const std::uint64_t GB = MB * 1024;
    static const std::uint64_t TB = GB * 1024;

    std::ostringstream ss;

    if (bytes < KB)
        ss << bytes << " B";
    else if (bytes < MB)
        ss << std::fixed << std::setprecision(decimals) << (double(bytes) / KB) << "KB";
    else if (bytes < GB)
        ss << std::fixed << std::setprecision(decimals) << (double(bytes) / MB) << "MB";
    else if (bytes < TB)
        ss << std::fixed << std::setprecision(decimals) << (double(bytes) / GB) << "GB";
    else
        ss << std::fixed << std::setprecision(decimals) << (double(bytes) / TB) << "TB";

    return ss.str();
}

}  // namespace Stockfish::Book

