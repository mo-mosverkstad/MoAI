#pragma once
#include "pipeline.h"
#include "../query/query_analyzer_factory.h"
#include "../retrieval/retriever_factory.h"
#include "../storage/segment_reader.h"
#include <memory>
#include <string>

class PipelineBuilder {
public:
    static Pipeline build(SegmentReader& reader,
                          const std::string& segdir,
                          const std::string& embeddir)
    {
        auto analyzer = QueryAnalyzerFactory::create(embeddir);
        auto retriever = RetrieverFactory::create(reader, embeddir);
        std::string conv_path = segdir + "/../.conversation";

        return Pipeline(
            std::move(analyzer),
            std::move(retriever),
            reader,
            conv_path
        );
    }
};
