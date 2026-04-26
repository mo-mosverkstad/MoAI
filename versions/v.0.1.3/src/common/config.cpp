#include "config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

Config& Config::instance() {
    static Config cfg;
    return cfg;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool Config::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (!key.empty()) values_[key] = val;
    }
    loaded_ = true;
    return true;
}

double Config::get_double(const std::string& key, double default_val) const {
    auto it = values_.find(key);
    if (it == values_.end()) return default_val;
    try { return std::stod(it->second); } catch (...) { return default_val; }
}

int Config::get_int(const std::string& key, int default_val) const {
    auto it = values_.find(key);
    if (it == values_.end()) return default_val;
    try { return std::stoi(it->second); } catch (...) { return default_val; }
}

size_t Config::get_size(const std::string& key, size_t default_val) const {
    auto it = values_.find(key);
    if (it == values_.end()) return default_val;
    try { return static_cast<size_t>(std::stoul(it->second)); } catch (...) { return default_val; }
}

std::string Config::get_string(const std::string& key, const std::string& default_val) const {
    auto it = values_.find(key);
    return (it != values_.end()) ? it->second : default_val;
}
