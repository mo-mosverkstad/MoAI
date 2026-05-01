#include "manifest.h"
#include "../common/file_utils.h"
#include <sstream>

Manifest::Manifest(const std::string& path) : path_(path) {
    load();
}

void Manifest::add_segment(const std::string& seg) {
    segs_.push_back(seg);
}

std::vector<std::string> Manifest::segments() const {
    return segs_;
}

void Manifest::save() const {
    std::vector<uint8_t> out;
    for (auto& s : segs_) {
        out.insert(out.end(), s.begin(), s.end());
        out.push_back('\n');
    }
    fileutils::write_file(path_, out);
}

void Manifest::load() {
    segs_.clear();
    auto data = fileutils::read_file(path_);
    if (data.empty()) return;

    std::stringstream ss(std::string(data.begin(), data.end()));
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty()) segs_.push_back(line);
    }
}