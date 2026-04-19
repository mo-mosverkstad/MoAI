#include "file_utils.h"
#include <fstream>
#include <filesystem>
#include <iostream>

namespace fileutils {

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

void write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        std::cerr << "write_file() failed: " << path << "\n";
        return;
    }
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
}

void append_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::app);
    if (!f) {
        std::cerr << "append_file() failed: " << path << "\n";
        return;
    }
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
}

bool ensure_dir(const std::string& path) {
    try {
        std::filesystem::create_directories(path);
        return true;
    } catch (...) {
        return false;
    }
}

}