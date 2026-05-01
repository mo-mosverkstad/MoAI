#include "vocab_loader.h"
#include <fstream>
#include <sstream>
#include <iostream>

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

VocabLoader::VocabMap VocabLoader::load(const std::string& path) {
    VocabMap result;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[WARN] Vocabulary file not found: " << path << "\n";
        return result;
    }
    std::string section, line;
    while (std::getline(f, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        if (section.empty()) continue;
        std::istringstream ss(t);
        std::string word;
        while (std::getline(ss, word, ',')) {
            std::string w = trim(word);
            if (!w.empty()) result[section].push_back(w);
        }
    }
    return result;
}

const VocabLoader::WordList& VocabLoader::get(
    const VocabMap& m, const std::string& section)
{
    static const WordList empty;
    auto it = m.find(section);
    return (it != m.end()) ? it->second : empty;
}
