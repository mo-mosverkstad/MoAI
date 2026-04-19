#pragma once
#include "../storage/segment_reader.h"
#include "../embedding/embedding_model.h"
#include "../embedding/vocab.h"
#include "../hnsw/hnsw_index.h"

class HybridBuilder {
public:
    HybridBuilder(SegmentReader& reader,
                  EmbeddingModel& model,
                  Vocabulary& vocab,
                  HNSWIndex& hnsw);

    void build();

    // Build vocab from segment, init random model, then build HNSW
    static void bootstrap(const std::string& segdir,
                          const std::string& embed_dir,
                          size_t hidden_dim = 64,
                          size_t output_dim = 32);

private:
    std::vector<float> doc_to_bow(DocID doc);

    SegmentReader& reader_;
    EmbeddingModel& model_;
    Vocabulary& vocab_;
    HNSWIndex& hnsw_;
};
