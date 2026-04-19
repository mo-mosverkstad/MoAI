#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <cmath>
#include "../storage/segment_reader.h"

/**
 * BM25 ranking engine.
 * Works on a *single segment* for this MVP.
 */
class BM25 {
public:
    BM25(SegmentReader& reader, double k1 = 1.2, double b = 0.75);

    /**
     * Search for top-K documents matching tokens.
     * Tokens must already be normalized (lowercase).
     */
    std::vector<std::pair<DocID, double>>
    search(const std::vector<std::string>& tokens, size_t topK);

    /** Score specific docs for given terms. */
    std::vector<std::pair<DocID, double>>
    score_docs(const std::vector<std::string>& terms,
               const std::vector<DocID>& docs);

private:
    double idf(const std::string& term);

private:
    SegmentReader& reader_;
    double k1_;
    double b_;
    double avgdl_;
    uint32_t N_;
};