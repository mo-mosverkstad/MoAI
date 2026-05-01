#include "vocab.h"
#include <fstream>
#include <algorithm>

bool Vocabulary::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    std::string w;
    int idx = 0;
    while (f >> w) {
        map_[w] = idx++;
    }
    return true;
}

void Vocabulary::build_from_terms(const std::vector<std::string>& terms) {
    map_.clear();
    int idx = 0;
    for (auto& t : terms) {
        if (map_.find(t) == map_.end()) {
            map_[t] = idx++;
        }
    }
}

bool Vocabulary::save(const std::string& path) const {
    // Sort by id to ensure deterministic order
    std::vector<std::pair<int, std::string>> sorted;
    for (auto& [term, id] : map_) sorted.push_back({id, term});
    std::sort(sorted.begin(), sorted.end());

    std::ofstream f(path);
    if (!f) return false;
    for (auto& [id, term] : sorted) f << term << "\n";
    return true;
}

int Vocabulary::id(const std::string& term) const {
    auto it = map_.find(term);
    return (it == map_.end()) ? -1 : it->second;
}
