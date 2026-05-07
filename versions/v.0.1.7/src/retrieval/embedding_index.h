#pragma once
#include "../hnsw/hnsw_index.h"
#include "../embedding/i_embedder.h"
#include "../embedding/embedder_factory.h"
#include "../storage/segment_reader.h"
#include <memory>
#include <iostream>
#include <string>
#include <vector>

// Shared HNSW + embedding infrastructure.
// Takes an IEmbedder — doesn't know or care which embedding method is used.
class EmbeddingIndex {
public:
    bool init(SegmentReader& reader, const std::string& embeddir) {
        embedder_ = EmbedderFactory::create(embeddir);
        if (!embedder_) return false;

        hnsw_ = std::make_unique<HNSWIndex>(
            static_cast<uint32_t>(embedder_->dim()), 16, 200, 100);
        uint32_t N = reader.doc_count();
        for (uint32_t d = 1; d <= N; d++)
            hnsw_->add_point(embedder_->embed(reader.get_document_text(d)));

        ready_ = true;
        std::cerr << "HNSW index built (" << embedder_->name() << " embedder)\n";
        return true;
    }

    std::vector<float> embed_query(const std::vector<std::string>& keywords) const {
        if (!embedder_) return {};
        std::string text;
        for (auto& kw : keywords) {
            if (!text.empty()) text += " ";
            text += kw;
        }
        return embedder_->embed(text);
    }

    bool ready() const { return ready_; }
    HNSWIndex* hnsw() const { return hnsw_.get(); }

private:
    std::unique_ptr<IEmbedder> embedder_;
    std::unique_ptr<HNSWIndex> hnsw_;
    bool ready_ = false;
};
