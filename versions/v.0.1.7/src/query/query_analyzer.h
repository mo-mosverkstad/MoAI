#pragma once
#include <string>
#include <vector>
#include <memory>
#include "information_need.h"
#include "i_query_analyzer.h"

// ── Legacy types (kept for neural analyzer compatibility) ──

enum class QueryIntent {
    FACTUAL,
    EXPLANATION,
    PROCEDURAL,
    COMPARISON,
    GENERAL
};

enum class AnswerType {
    LOCATION,
    DEFINITION,
    PERSON_PROFILE,
    TEMPORAL,
    PROCEDURE,
    COMPARISON,
    SUMMARY
};

struct QueryAnalysis {
    QueryIntent intent;
    AnswerType answerType;
    std::string mainEntity;
    std::vector<std::string> keywords;
};

// ── Rule-based analyzer ──

class RuleBasedQueryAnalyzer : public IQueryAnalyzer {
public:
    std::vector<InformationNeed> analyze(const std::string& query) override;
    std::string name() const override { return "rule"; }

    QueryAnalysis analyze_legacy(const std::string& query);

private:
    std::vector<std::string> split_clauses(const std::string& query) const;
    std::string extract_entity(const std::string& clause,
                               const std::vector<std::string>& keywords,
                               const std::string& original_query) const;
    Property detect_property(const std::string& clause) const;
    AnswerForm detect_form(const std::string& clause, Property prop) const;
    AnswerScope infer_scope(const std::string& clause, AnswerForm form) const;
    std::vector<std::string> extract_keywords(const std::string& clause) const;
};
