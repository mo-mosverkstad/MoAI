#include "index_builder.h"
#include "../common/file_utils.h"
#include <filesystem>
#include <iostream>

IndexBuilder::IndexBuilder(const std::string& segdir)
    : writer_(segdir)
{}

void IndexBuilder::ingest_directory(const std::string& dirpath) {
    for (auto& p : std::filesystem::recursive_directory_iterator(dirpath)) {
        if (!p.is_regular_file()) continue;
        ingest_file(p.path().string());
    }
}

void IndexBuilder::ingest_file(const std::string& path) {
    auto data = fileutils::read_file(path);
    if (data.empty()) return;

    std::string text(data.begin(), data.end());
    auto tokens = tokenizer_.tokenize(text);

    writer_.add_document(text, tokens);
}

void IndexBuilder::finalize() {
    writer_.finalize();
}