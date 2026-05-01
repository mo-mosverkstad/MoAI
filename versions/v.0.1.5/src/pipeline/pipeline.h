#pragma once
#include "../query/i_query_analyzer.h"
#include "../retrieval/i_retriever.h"
#include "../chunk/chunker.h"
#include "../answer/answer_synthesizer.h"
#include "../answer/answer_validator.h"
#include "../answer/answer_compressor.h"
#include "../answer/answer_scope.h"
#include "../answer/question_planner.h"
#include "../answer/self_ask.h"
#include "../conversation/conversation_state.h"
#include "../storage/segment_reader.h"
#include "../common/config.h"
#include <memory>
#include <string>
#include <vector>

struct PipelineOptions {
    bool json = false;
    bool force_brief = false;
    bool force_detailed = false;
};

struct PipelineResult {
    std::vector<InformationNeed> needs;
    CompositeAnswer composite;
    std::string retriever_name;
};

class Pipeline {
public:
    Pipeline(std::unique_ptr<IQueryAnalyzer> analyzer,
             std::unique_ptr<IRetriever> retriever,
             SegmentReader& reader,
             const std::string& conv_path)
        : analyzer_(std::move(analyzer))
        , retriever_(std::move(retriever))
        , reader_(reader)
        , conv_path_(conv_path)
    {
        auto& cfg = Config::instance();
        max_evidence_ = cfg.get_size("retrieval.max_evidence", 15);
        chunks_per_doc_ = cfg.get_size("chunk.max_per_doc", 8);
        support_thr_ = cfg.get_double("validator.support_coverage_threshold", 0.5);
    }

    PipelineResult run(const std::string& query, const PipelineOptions& opts);

    std::string retriever_name() const { return retriever_->name(); }

private:
    std::unique_ptr<IQueryAnalyzer> analyzer_;
    std::unique_ptr<IRetriever> retriever_;
    SegmentReader& reader_;
    std::string conv_path_;

    Chunker chunker_;
    AnswerSynthesizer synthesizer_;
    AnswerValidator validator_;
    AgreementCompressor compressor_;
    SelfAskModule self_ask_;
    QuestionPlanner planner_;

    size_t max_evidence_;
    size_t chunks_per_doc_;
    double support_thr_;
};
