#include "query_analyzer.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <sstream>

#ifdef HAS_TORCH
#include "neural_query_analyzer.h"
#include <filesystem>
#endif

// ── Stop words ──

static const std::unordered_set<std::string>& stop_words() {
    static const std::unordered_set<std::string> sw = {
        "a", "an", "the", "is", "are", "was", "were", "be", "been", "being",
        "have", "has", "had", "do", "does", "did", "will", "would", "shall",
        "should", "may", "might", "must", "can", "could",
        "i", "me", "my", "we", "our", "you", "your", "he", "she", "it",
        "they", "them", "their", "this", "that", "these", "those",
        "what", "which", "who", "whom", "how", "when", "where", "why",
        "not", "no", "nor", "but", "and", "or", "if", "then", "so",
        "at", "by", "for", "with", "about", "from", "to", "in", "on",
        "of", "as", "into", "through", "during", "before", "after",
        "tell", "me", "explain", "describe", "define", "show",
        "please", "could", "would", "about"
    };
    return sw;
}

static std::string to_lower(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return r;
}

static bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

static bool contains(const std::string& s, const std::string& sub) {
    return s.find(sub) != std::string::npos;
}

// ── RuleBasedQueryAnalyzer ──

QueryIntent RuleBasedQueryAnalyzer::detect_intent(const std::string& lower) const {
    if (starts_with(lower, "where") || starts_with(lower, "what") ||
        starts_with(lower, "who") || starts_with(lower, "when"))
        return QueryIntent::FACTUAL;

    if (starts_with(lower, "how") || starts_with(lower, "why") ||
        contains(lower, "explain") || contains(lower, "describe"))
        return QueryIntent::EXPLANATION;

    if (contains(lower, "how to") || contains(lower, "steps to") ||
        contains(lower, "procedure"))
        return QueryIntent::PROCEDURAL;

    if (contains(lower, "difference between") || contains(lower, " vs ") ||
        contains(lower, "compare") || contains(lower, "versus"))
        return QueryIntent::COMPARISON;

    return QueryIntent::GENERAL;
}

AnswerType RuleBasedQueryAnalyzer::detect_answer_type(const std::string& lower,
                                              QueryIntent intent) const {
    if (starts_with(lower, "where") || contains(lower, "located") ||
        contains(lower, "location of"))
        return AnswerType::LOCATION;

    if (starts_with(lower, "who") || contains(lower, "person") ||
        contains(lower, "inventor") || contains(lower, "founder"))
        return AnswerType::PERSON_PROFILE;

    if (starts_with(lower, "when") || contains(lower, "what year") ||
        contains(lower, "what date"))
        return AnswerType::TEMPORAL;

    if (intent == QueryIntent::COMPARISON)
        return AnswerType::COMPARISON;

    if (intent == QueryIntent::PROCEDURAL || contains(lower, "how to") ||
        contains(lower, "how does") || contains(lower, "how do"))
        return AnswerType::PROCEDURE;

    if (starts_with(lower, "what") || contains(lower, "define") ||
        contains(lower, "definition") || contains(lower, "what is") ||
        contains(lower, "what are"))
        return AnswerType::DEFINITION;

    return AnswerType::SUMMARY;
}

std::vector<std::string> RuleBasedQueryAnalyzer::extract_keywords(const std::string& lower) const {
    auto& sw = stop_words();
    std::vector<std::string> keywords;
    std::string word;

    for (char c : lower) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            word.push_back(c);
        } else {
            if (!word.empty() && word.size() > 1 && sw.find(word) == sw.end())
                keywords.push_back(word);
            word.clear();
        }
    }
    if (!word.empty() && word.size() > 1 && sw.find(word) == sw.end())
        keywords.push_back(word);

    return keywords;
}

std::string RuleBasedQueryAnalyzer::extract_main_entity(
    const std::string& lower,
    const std::vector<std::string>& keywords) const
{
    if (keywords.empty()) return "";

    std::string best = keywords[0];
    for (auto& k : keywords) {
        if (k.size() > best.size()) best = k;
    }
    return best;
}

QueryAnalysis RuleBasedQueryAnalyzer::analyze(const std::string& query) const {
    std::string lower = to_lower(query);

    QueryAnalysis qa;
    qa.intent = detect_intent(lower);
    qa.answerType = detect_answer_type(lower, qa.intent);
    qa.keywords = extract_keywords(lower);
    qa.mainEntity = extract_main_entity(lower, qa.keywords);

    return qa;
}

// ── QueryAnalyzer (unified dispatch) ──

#ifdef HAS_TORCH
struct QueryAnalyzer::NeuralImpl {
    Vocabulary vocab;
    NeuralQueryAnalyzer analyzer;
    NeuralImpl(Vocabulary v, int64_t dim, int64_t max_len)
        : vocab(std::move(v)), analyzer(vocab, dim, max_len) {}
};
#else
struct QueryAnalyzer::NeuralImpl {};
#endif

QueryAnalyzer::QueryAnalyzer() = default;
QueryAnalyzer::~QueryAnalyzer() = default;

bool QueryAnalyzer::load_neural(const std::string& model_path,
                                 const std::string& vocab_path) {
#ifdef HAS_TORCH
    if (!std::filesystem::exists(model_path) ||
        !std::filesystem::exists(vocab_path))
        return false;

    Vocabulary vocab;
    if (!vocab.load(vocab_path)) return false;

    auto impl = std::make_unique<NeuralImpl>(std::move(vocab), 128, 64);
    impl->analyzer.load(model_path);
    neural_ = std::move(impl);
    use_neural_ = true;
    return true;
#else
    (void)model_path;
    (void)vocab_path;
    return false;
#endif
}

QueryAnalysis QueryAnalyzer::analyze(const std::string& query) const {
#ifdef HAS_TORCH
    if (use_neural_ && neural_)
        return neural_->analyzer.analyze(query);
#endif
    return rule_based_.analyze(query);
}
