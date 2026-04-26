#pragma once
#include "../embedding/embedding_model.h"
#include "../embedding/vocab.h"
#include "../hnsw/hnsw_index.h"
#include "../inverted/bm25.h"
#include "../inverted/tokenizer.h"
#include "../storage/segment_reader.h"
#include "../summarizer/summarizer.h"

class HybridSearch {
public:
    HybridSearch(SegmentReader& reader,
                 EmbeddingModel& model,
                 Vocabulary& vocab,
                 HNSWIndex& hnsw);

    struct Result {
        uint32_t doc = 0;
        double score = 0.0;
        double bm25 = 0.0;
        double ann = 0.0;
    };

    std::vector<Result> search(const std::string& query,
                               size_t topk_bm25,
                               size_t topk_ann,
                               bool json);

    std::string summarize_results(const std::vector<Result>& results,
                                   const std::string& query);

private:
    std::vector<float> embed_query(const std::string& q);

    SegmentReader& reader_;
    EmbeddingModel& model_;
    Vocabulary& vocab_;
    HNSWIndex& hnsw_;
};
