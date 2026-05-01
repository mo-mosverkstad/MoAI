#pragma once
#include "i_retriever.h"
#include "../inverted/bm25.h"
#include "../common/config.h"

class BM25Retriever : public IRetriever {
public:
    explicit BM25Retriever(SegmentReader& reader)
        : bm25_(reader)
        , top_k_(Config::instance().get_size("bm25.top_k", 10))
        , max_docs_(Config::instance().get_size("retrieval.max_ranked_docs", 10))
    {}

    std::vector<ScoredDoc> search(const std::vector<std::string>& keywords) override {
        auto results = bm25_.search(keywords, top_k_);
        std::vector<ScoredDoc> out;
        for (auto& [docId, score] : results)
            out.push_back({docId, score});
        if (out.size() > max_docs_) out.resize(max_docs_);
        return out;
    }

    std::string name() const override { return "bm25"; }

    BM25& bm25() { return bm25_; }

private:
    BM25 bm25_;
    size_t top_k_;
    size_t max_docs_;
};
