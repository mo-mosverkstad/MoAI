#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>

struct PhraseMatchInfo {
    bool matches = false;
};

class PhraseMatcher {
public:
    /**
     * postings = vector of <docID, vector-of-positions>
     * Returns true if phrase occurs in the document.
     */
    static bool match(
        const std::vector<std::vector<uint32_t>>& pos_lists
    );
};