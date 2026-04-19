#pragma once
#include <string>
#include <vector>

/**
 * Manifest tracks all segments in the index.
 */
class Manifest {
public:
    explicit Manifest(const std::string& path);

    void add_segment(const std::string& seg);
    std::vector<std::string> segments() const;

    void save() const;
    void load();

private:
    std::string path_;
    std::vector<std::string> segs_;
};