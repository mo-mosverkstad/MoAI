#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace fileutils {

/**
 * Read entire file into memory.
 */
std::vector<uint8_t> read_file(const std::string& path);

/**
 * Write buffer to file (overwrite).
 */
void write_file(const std::string& path, const std::vector<uint8_t>& data);

/**
 * Append raw bytes to file.
 */
void append_file(const std::string& path, const std::vector<uint8_t>& data);

/**
 * Create directory recursively (MVP implementation).
 */
bool ensure_dir(const std::string& path);

}