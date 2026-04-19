#pragma once
#include <string>
#include <vector>
#include "../query/query_analyzer.h"
#include "../chunk/chunker.h"

struct Evidence {
    uint32_t docId;
    ChunkType type;
    std::string text;
    double score;
};

struct Answer {
    std::string text;
    double confidence;
    AnswerType answerType;
};

class AnswerSynthesizer {
public:
    Answer synthesize(const QueryAnalysis& analysis,
                      const std::vector<Evidence>& evidence) const;

private:
    Answer synthesize_location(const QueryAnalysis& qa,
                                const std::vector<Evidence>& evidence) const;
    Answer synthesize_definition(const QueryAnalysis& qa,
                                  const std::vector<Evidence>& evidence) const;
    Answer synthesize_person(const QueryAnalysis& qa,
                              const std::vector<Evidence>& evidence) const;
    Answer synthesize_temporal(const QueryAnalysis& qa,
                                const std::vector<Evidence>& evidence) const;
    Answer synthesize_summary(const QueryAnalysis& qa,
                               const std::vector<Evidence>& evidence) const;

    // Extract best sentences matching keywords from evidence text
    std::vector<std::string> extract_sentences(
        const std::string& text,
        const std::vector<std::string>& keywords,
        size_t max_count) const;

    // Extract sentences with year-aware scoring for temporal queries
    std::vector<std::string> extract_temporal_sentences(
        const std::string& text,
        const std::vector<std::string>& keywords,
        const std::string& entity,
        size_t max_count) const;

    double compute_confidence(const std::vector<Evidence>& evidence,
                               const std::vector<std::string>& keywords) const;
};
