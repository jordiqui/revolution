#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Stockfish::Book {

// Configure the base directory used to resolve relative book paths.
void set_base_directory(std::string directory);
const std::string& base_directory();

std::string format_option_key(const char* pattern, int index);

bool is_empty_filename(const std::string& filename);
std::string map_path(const std::string& path);
bool is_same_file(const std::string& lhs, const std::string& rhs);
std::size_t get_file_size(const std::string& path);
std::string format_bytes(std::uint64_t bytes, int decimals);

}  // namespace Stockfish::Book

