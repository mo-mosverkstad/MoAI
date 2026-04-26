#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct ScoredDoc {
    uint32_t docId;
    double score;
};

struct IRetriever {
    virtual ~IRetriever() = default;

    // Search for documents matching the given keywords.
    // Returns ranked list of (docId, score) pairs.
    virtual std::vector<ScoredDoc>
    search(const std::vector<std::string>& keywords) = 0;

    // Name of this retriever (for logging/JSON output)
    virtual std::string name() const = 0;

    // Whether this retriever has a simpler fallback mode
    virtual bool supports_fallback() const { return false; }

    // Fallback search (e.g., BM25-only when hybrid fails)
    virtual std::vector<ScoredDoc>
    fallback_search(const std::vector<std::string>& keywords) {
        return search(keywords);
    }
};
