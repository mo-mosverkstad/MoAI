#pragma once
#include <string>
#include <vector>
#include <memory>

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

// Rule-based analyzer (always available)
class RuleBasedQueryAnalyzer {
public:
    QueryAnalysis analyze(const std::string& query) const;

private:
    QueryIntent detect_intent(const std::string& lower) const;
    AnswerType detect_answer_type(const std::string& lower, QueryIntent intent) const;
    std::string extract_main_entity(const std::string& lower,
                                     const std::vector<std::string>& keywords) const;
    std::vector<std::string> extract_keywords(const std::string& lower) const;
};

// Unified analyzer: delegates to neural if loaded, else rule-based
class QueryAnalyzer {
public:
    QueryAnalyzer();
    ~QueryAnalyzer();

    // Try to load neural model; returns true on success
    bool load_neural(const std::string& model_path,
                     const std::string& vocab_path);

    bool is_neural() const { return use_neural_; }

    QueryAnalysis analyze(const std::string& query) const;

private:
    RuleBasedQueryAnalyzer rule_based_;
    bool use_neural_ = false;

    // Opaque pointer to avoid torch headers leaking into non-torch builds
    struct NeuralImpl;
    std::unique_ptr<NeuralImpl> neural_;
};
