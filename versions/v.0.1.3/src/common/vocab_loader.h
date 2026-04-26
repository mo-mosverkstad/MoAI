#pragma once
#include <string>
#include <vector>
#include <unordered_map>

// Loads vocabulary files with format:
//   [SECTION_NAME]
//   word1, word2, word3
//   word4, word5
//   [ANOTHER_SECTION]
//   ...
// Lines starting with # are comments.

class VocabLoader {
public:
    using WordList = std::vector<std::string>;
    using VocabMap = std::unordered_map<std::string, WordList>;

    // Load vocabulary file. Logs warning to stderr if file is missing.
    static VocabMap load(const std::string& path);

    // Get a section, returns empty vector if section not found.
    static const WordList& get(const VocabMap& m, const std::string& section);
};
