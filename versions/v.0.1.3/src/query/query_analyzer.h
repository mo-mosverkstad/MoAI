#pragma once
#include <string>
#include <vector>
#include <memory>
#include "information_need.h"

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

// ── Rule-based analyzer: produces InformationNeeds directly ──

class RuleBasedQueryAnalyzer {
public:
    std::vector<InformationNeed> analyze(const std::string& query) const;

    // Legacy adapter
    QueryAnalysis analyze_legacy(const std::string& query) const;

private:
    // Split query into sub-clauses (e.g. "where is X and why is it important")
    std::vector<std::string> split_clauses(const std::string& query) const;

    // Extract entity from a clause
    std::string extract_entity(const std::string& clause,
                               const std::vector<std::string>& keywords) const;

    // Detect property from semantic signals (not just interrogatives)
    Property detect_property(const std::string& clause) const;

    // Infer answer form from property and clause structure
    AnswerForm detect_form(const std::string& clause, Property prop) const;

    // Infer answer scope from query wording and form
    AnswerScope infer_scope(const std::string& clause, AnswerForm form) const;

    // Extract content keywords (non-stop words)
    std::vector<std::string> extract_keywords(const std::string& clause) const;
};

// ── Unified analyzer: delegates to neural if loaded, else rule-based ──

class QueryAnalyzer {
public:
    QueryAnalyzer();
    ~QueryAnalyzer();

    bool load_neural(const std::string& model_path,
                     const std::string& vocab_path);

    bool is_neural() const { return use_neural_; }

    // Primary interface: one query → multiple InformationNeeds
    std::vector<InformationNeed> analyze(const std::string& query) const;

    // Legacy interface for backward compatibility
    QueryAnalysis analyze_legacy(const std::string& query) const;

private:
    RuleBasedQueryAnalyzer rule_based_;
    bool use_neural_ = false;

    struct NeuralImpl;
    std::unique_ptr<NeuralImpl> neural_;
};
