#pragma once
#include "i_retriever.h"
#include "embedding_index.h"
#include "../common/config.h"
#include <algorithm>

class HNSWRetriever : public IRetriever {
public:
    HNSWRetriever(SegmentReader& reader, const std::string& embeddir) {
        max_docs_ = Config::instance().get_size("retrieval.max_ranked_docs", 10);
        if (!index_.init(reader, embeddir))
            std::cerr << "[WARN] HNSW retriever requested but no embeddings found\n";
        std::cerr << "Using HNSW-only retrieval\n";
    }

    std::vector<ScoredDoc> search(const std::vector<std::string>& keywords) override {
        if (!index_.ready()) return {};
        auto q_emb = index_.embed_query(keywords);
        if (q_emb.empty()) return {};

        auto ann_ids = index_.hnsw()->search(q_emb, max_docs_);
        std::vector<ScoredDoc> out;
        for (uint32_t id : ann_ids) {
            const auto& vec = index_.hnsw()->get_vector(id);
            float dot = 0.0f;
            for (size_t i = 0; i < q_emb.size(); i++)
                dot += q_emb[i] * vec[i];
            out.push_back({id + 1, (static_cast<double>(dot) + 1.0) * 0.5});
        }
        std::sort(out.begin(), out.end(),
                  [](auto& a, auto& b) { return a.score > b.score; });
        if (out.size() > max_docs_) out.resize(max_docs_);
        return out;
    }

    std::string name() const override { return "hnsw"; }

private:
    EmbeddingIndex index_;
    size_t max_docs_;
};
