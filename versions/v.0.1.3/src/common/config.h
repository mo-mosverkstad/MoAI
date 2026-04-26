#pragma once
#include <string>
#include <unordered_map>

class Config {
public:
    static Config& instance();

    bool load(const std::string& path);
    bool loaded() const { return loaded_; }

    double get_double(const std::string& key, double default_val) const;
    int get_int(const std::string& key, int default_val) const;
    size_t get_size(const std::string& key, size_t default_val) const;
    std::string get_string(const std::string& key, const std::string& default_val = "") const;

private:
    Config() = default;
    std::unordered_map<std::string, std::string> values_;
    bool loaded_ = false;
};
