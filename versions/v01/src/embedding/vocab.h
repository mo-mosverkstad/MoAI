#pragma once
#include <string>
#include <unordered_map>
#include <vector>

class Vocabulary {
public:
    bool load(const std::string& path);
    void build_from_terms(const std::vector<std::string>& terms);

    int id(const std::string& term) const;
    size_t size() const { return map_.size(); }

    bool save(const std::string& path) const;

private:
    std::unordered_map<std::string, int> map_;
};
