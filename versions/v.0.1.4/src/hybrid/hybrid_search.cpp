#include "hybrid_search.h"
#include "../common/config.h"
#include <unordered_map>
#include <iostream>
#include <algorithm>
#include <cmath>

HybridSearch::HybridSearch(SegmentReader& reader,
                           EmbeddingModel& model,
                           Vocabulary& vocab,
                           HNSWIndex& hnsw)
    : reader_(reader), model_(model), vocab_(vocab), hnsw_(hnsw)
{}

std::vector<float> HybridSearch::embed_query(const std::string& q) {
    Tokenizer tok;
    auto tokens = tok.tokenize(q);
    std::vector<float> bow(vocab_.size(), 0.0f);

    for (auto& t : tokens) {
        int vid = vocab_.id(t);
        if (vid >= 0) bow[vid] += 1.0f;
    }
    return model_.embed(bow);
}

std::string HybridSearch::summarize_results(
    const std::vector<Result>& results,
    const std::string& query)
{
    Tokenizer tok;
    auto query_tokens = tok.tokenize(query);

    // Load document texts from segment
    std::unordered_map<uint32_t, std::string> doc_text;
    std::vector<std::pair<uint32_t, double>> simple;

    for (auto& r : results) {
        std::string text = reader_.get_document_text(r.doc);
        if (!text.empty())
            doc_text[r.doc] = text;
        simple.push_back({r.doc, r.score});
    }

    Summarizer summarizer;
    return summarizer.summarize(simple, doc_text, query_tokens);
}

std::vector<HybridSearch::Result>
HybridSearch::search(const std::string& query,
                     size_t topk_bm25,
                     size_t topk_ann,
                     bool json)
{
    // BM25 retrieval
    BM25 bm25(reader_);
    Tokenizer tok;
    auto toks = tok.tokenize(query);
    auto bm25_results = bm25.search(toks, topk_bm25);

    // ANN retrieval
    auto q_emb = embed_query(query);
    auto ann_ids = hnsw_.search(q_emb, topk_ann);

    // Merge results
    std::unordered_map<uint32_t, Result> merged;

    for (auto& [doc, score] : bm25_results) {
        merged[doc].doc = doc;
        merged[doc].bm25 = score;
    }

    for (uint32_t id : ann_ids) {
        uint32_t doc = id + 1;
        const auto& vec = hnsw_.get_vector(id);
        float dot = 0.0f;
        for (size_t i = 0; i < q_emb.size(); i++)
            dot += q_emb[i] * vec[i];
        double ann_score = static_cast<double>(std::max(0.0f, dot));

        merged[doc].doc = doc;
        merged[doc].ann = ann_score;
    }

    std::vector<Result> out;
    out.reserve(merged.size());

    // Normalize BM25 scores for proper fusion
    double max_bm25 = 0.0;
    for (auto& [id, r] : merged)
        if (r.bm25 > max_bm25) max_bm25 = r.bm25;

    for (auto& [id, r] : merged) {
        // Normalize: bm25 to [0,1], ann cosine to [0,1]
        double bm25_norm = (max_bm25 > 0.0) ? r.bm25 / max_bm25 : 0.0;
        double ann_norm = (r.ann + 1.0) * 0.5;  // cosine [-1,1] -> [0,1]
        double w_bm25 = Config::instance().get_double("retrieval.bm25_weight", 0.7);
        double w_ann  = Config::instance().get_double("retrieval.ann_weight", 0.3);
        r.score = w_bm25 * bm25_norm + w_ann * ann_norm;
        out.push_back(r);
    }

    std::sort(out.begin(), out.end(),
              [](auto& a, auto& b) { return a.score > b.score; });

    if (json) {
        std::string summary = summarize_results(out, query);

        std::cout << "{\n  \"results\": [\n";
        for (size_t i = 0; i < out.size(); i++) {
            auto& r = out[i];
            std::cout << "    { \"doc\": " << r.doc
                      << ", \"score\": " << r.score
                      << ", \"bm25\": " << r.bm25
                      << ", \"ann\": " << r.ann
                      << " }";
            if (i + 1 < out.size()) std::cout << ",";
            std::cout << "\n";
        }
        std::cout << "  ],\n";

        // Escape quotes in summary for valid JSON
        std::string escaped;
        for (char c : summary) {
            if (c == '"') escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else if (c == '\n') escaped += "\\n";
            else if (c == '\r') continue;
            else escaped += c;
        }
        std::cout << "  \"summary\": \"" << escaped << "\"\n";
        std::cout << "}\n";
    }

    return out;
}
