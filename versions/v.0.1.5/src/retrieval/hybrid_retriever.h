#pragma once
#include "i_retriever.h"
#include "embedding_index.h"
#include "../inverted/bm25.h"
#include "../common/config.h"
#include <unordered_map>
#include <algorithm>

class HybridRetriever : public IRetriever {
public:
    HybridRetriever(SegmentReader& reader, const std::string& embeddir)
        : bm25_(reader)
    {
        auto& cfg = Config::instance();
        top_k_    = cfg.get_size("bm25.top_k", 10);
        max_docs_ = cfg.get_size("retrieval.max_ranked_docs", 10);
        bm25_w_   = cfg.get_double("retrieval.bm25_weight", 0.7);
        ann_w_    = cfg.get_double("retrieval.ann_weight", 0.3);

        if (index_.init(reader, embeddir))
            std::cerr << "Using hybrid retrieval (BM25 + HNSW)\n";
    }

    std::vector<ScoredDoc> search(const std::vector<std::string>& keywords) override {
        auto bm25_results = bm25_.search(keywords, top_k_);

        std::unordered_map<uint32_t, double> doc_scores;
        for (auto& [docId, score] : bm25_results)
            doc_scores[docId] = score;

        if (index_.ready()) {
            auto q_emb = index_.embed_query(keywords);
            if (!q_emb.empty()) {
                auto ann_ids = index_.hnsw()->search(q_emb, top_k_);

                double max_bm25 = 0.0;
                for (auto& [id, sc] : doc_scores)
                    if (sc > max_bm25) max_bm25 = sc;

                for (uint32_t id : ann_ids) {
                    uint32_t doc = id + 1;
                    if (!doc_scores.count(doc)) continue;
                    const auto& vec = index_.hnsw()->get_vector(id);
                    float dot = 0.0f;
                    for (size_t i = 0; i < q_emb.size(); i++)
                        dot += q_emb[i] * vec[i];
                    double ann_norm = (static_cast<double>(dot) + 1.0) * 0.5;
                    double bm25_norm = (max_bm25 > 0) ? doc_scores[doc] / max_bm25 : 0.0;
                    doc_scores[doc] = bm25_w_ * bm25_norm + ann_w_ * ann_norm;
                }

                for (auto& [doc, sc] : doc_scores) {
                    if (max_bm25 > 0) {
                        bool was_ann = false;
                        for (uint32_t id : ann_ids)
                            if (id + 1 == doc) { was_ann = true; break; }
                        if (!was_ann) sc = bm25_w_ * (sc / max_bm25);
                    }
                }
            }
        }

        std::vector<ScoredDoc> ranked;
        for (auto& [docId, score] : doc_scores)
            ranked.push_back({docId, score});
        std::sort(ranked.begin(), ranked.end(),
                  [](auto& a, auto& b) { return a.score > b.score; });
        if (ranked.size() > max_docs_) ranked.resize(max_docs_);
        return ranked;
    }

    std::string name() const override {
        return index_.ready() ? "hybrid" : "bm25";
    }

    bool supports_fallback() const override { return index_.ready(); }

    std::vector<ScoredDoc> fallback_search(const std::vector<std::string>& keywords) override {
        auto results = bm25_.search(keywords, top_k_);
        std::vector<ScoredDoc> out;
        for (auto& [docId, score] : results)
            out.push_back({docId, score});
        if (out.size() > max_docs_) out.resize(max_docs_);
        return out;
    }

private:
    BM25 bm25_;
    EmbeddingIndex index_;
    size_t top_k_;
    size_t max_docs_;
    double bm25_w_, ann_w_;
};
