#pragma once
#include <string>
#include <vector>
#include "../query/information_need.h"
#include "answer_synthesizer.h"

struct EvidenceAnalysis {
    double agreement;           // average agreement score (0.0-1.0)
    int agreement_pairs;        // number of agreeing pairs
    int contradiction_pairs;    // number of contradicting pairs
    double confidence_penalty;  // total penalty from contradictions
};

class AnswerValidator {
public:
    // Self-ask validation: does the answer address the property?
    void validate(Answer& answer, const InformationNeed& need) const;

    // Analyze evidence for agreement and contradictions
    EvidenceAnalysis analyze_evidence(const std::vector<Evidence>& evidence,
                                      const std::string& entity) const;

    // Compute refined confidence using agreement + contradiction + coverage
    double compute_refined_confidence(const std::vector<Evidence>& evidence,
                                       const std::string& entity,
                                       const std::vector<std::string>& keywords) const;
};
