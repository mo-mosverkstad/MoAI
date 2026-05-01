#pragma once
#include "i_retriever.h"
#include "bm25_retriever.h"
#include "hnsw_retriever.h"
#include "hybrid_retriever.h"
#include "../common/config.h"
#include "../storage/segment_reader.h"
#include <memory>
#include <stdexcept>
#include <iostream>

class RetrieverFactory {
public:
    static std::unique_ptr<IRetriever> create(
        SegmentReader& reader,
        const std::string& embeddir)
    {
        auto& cfg = Config::instance();
        std::string type = cfg.get_string("retrieval.retriever", "hybrid");

        if (type == "bm25") {
            std::cerr << "Using BM25-only retrieval (config)\n";
            return std::make_unique<BM25Retriever>(reader);
        }

        if (type == "hnsw") {
            return std::make_unique<HNSWRetriever>(reader, embeddir);
        }

        if (type == "hybrid") {
            return std::make_unique<HybridRetriever>(reader, embeddir);
        }

        throw std::runtime_error("Unknown retriever: " + type +
            " (valid: bm25, hnsw, hybrid)");
    }
};
