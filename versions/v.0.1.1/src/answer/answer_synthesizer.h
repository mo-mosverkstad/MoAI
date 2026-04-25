#pragma once
#include <string>
#include <vector>
#include "../query/information_need.h"
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
    Property property;
    std::vector<uint32_t> sources;  // doc IDs used as evidence
};

struct CompositeAnswer {
    std::vector<Answer> parts;
    std::string merged_text() const;
    double overall_confidence() const;
};

class AnswerSynthesizer {
public:
    Answer synthesize(const InformationNeed& need,
                      const std::vector<Evidence>& evidence) const;

private:
    Answer synthesize_location(const InformationNeed& need,
                               const std::vector<Evidence>& evidence) const;
    Answer synthesize_definition(const InformationNeed& need,
                                 const std::vector<Evidence>& evidence) const;
    Answer synthesize_temporal(const InformationNeed& need,
                               const std::vector<Evidence>& evidence) const;
    Answer synthesize_explanation(const InformationNeed& need,
                                  const std::vector<Evidence>& evidence) const;
    Answer synthesize_list(const InformationNeed& need,
                           const std::vector<Evidence>& evidence) const;
    Answer synthesize_advantages(const InformationNeed& need,
                                 const std::vector<Evidence>& evidence) const;
    Answer synthesize_limitations(const InformationNeed& need,
                                  const std::vector<Evidence>& evidence) const;
    Answer synthesize_usage(const InformationNeed& need,
                            const std::vector<Evidence>& evidence) const;
    Answer synthesize_history(const InformationNeed& need,
                              const std::vector<Evidence>& evidence) const;
    Answer synthesize_comparison(const InformationNeed& need,
                                 const std::vector<Evidence>& evidence) const;
    Answer synthesize_general(const InformationNeed& need,
                              const std::vector<Evidence>& evidence) const;

    std::vector<std::string> extract_sentences(
        const std::string& text,
        const std::vector<std::string>& keywords,
        size_t max_count) const;

    std::vector<std::string> extract_temporal_sentences(
        const std::string& text,
        const std::vector<std::string>& keywords,
        const std::string& entity,
        size_t max_count) const;

    double compute_confidence(const std::vector<Evidence>& evidence,
                               const std::vector<std::string>& keywords) const;
};
