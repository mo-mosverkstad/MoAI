#include "query_analyzer.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <sstream>

#ifdef HAS_TORCH
#include "neural_query_analyzer.h"
#include <filesystem>
#endif

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

static bool contains(const std::string& s, const std::string& sub) {
    return s.find(sub) != std::string::npos;
}

// ── RuleBasedQueryAnalyzer ──

std::vector<std::string> RuleBasedQueryAnalyzer::split_clauses(const std::string& query) const {
    std::string lower = to_lower(query);
    std::vector<std::string> clauses;
    // Split on " and ", " also ", semicolons, question marks
    std::string current;
    size_t i = 0;
    while (i < lower.size()) {
        // Check for clause separators
        bool split = false;
        if (lower[i] == ';' || lower[i] == '?') {
            split = true;
        } else if (i + 5 <= lower.size() && lower.substr(i, 5) == " and ") {
            // Only split on " and " if followed by a question word or verb
            size_t after = i + 5;
            while (after < lower.size() && lower[after] == ' ') after++;
            std::string rest = lower.substr(after);
            if (contains(rest, "where") || contains(rest, "what") ||
                contains(rest, "why") || contains(rest, "how") ||
                contains(rest, "when") || contains(rest, "who") ||
                rest.substr(0, 4) == "also" || rest.substr(0, 4) == "tell") {
                split = true;
                i += 4; // skip " and"
            }
        }
        if (split) {
            if (!current.empty()) {
                size_t s = current.find_first_not_of(" \t");
                if (s != std::string::npos) clauses.push_back(current.substr(s));
            }
            current.clear();
        } else {
            current.push_back(lower[i]);
        }
        i++;
    }
    if (!current.empty()) {
        size_t s = current.find_first_not_of(" \t");
        if (s != std::string::npos) clauses.push_back(current.substr(s));
    }
    if (clauses.empty()) clauses.push_back(to_lower(query));
    return clauses;
}

std::vector<std::string> RuleBasedQueryAnalyzer::extract_keywords(const std::string& clause) const {
    auto& sw = stop_words();
    std::vector<std::string> keywords;
    std::string word;
    for (char c : clause) {
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

std::string RuleBasedQueryAnalyzer::extract_entity(
    const std::string& clause, const std::vector<std::string>& keywords) const
{
    if (keywords.empty()) return "";
    std::string best = keywords[0];
    for (auto& k : keywords)
        if (k.size() > best.size()) best = k;
    return best;
}

Property RuleBasedQueryAnalyzer::detect_property(const std::string& clause) const {
    // Semantic signal groups — not dependent on interrogative position
    if (contains(clause, "locat") || contains(clause, "where") ||
        contains(clause, "capital") || contains(clause, "coast") ||
        contains(clause, "close to") || contains(clause, "near") ||
        contains(clause, "region") || contains(clause, "country") ||
        contains(clause, "city") || contains(clause, "geograph"))
        return Property::LOCATION;

    if (contains(clause, "vs ") || contains(clause, "versus") ||
        contains(clause, "compare") || contains(clause, "difference between") ||
        contains(clause, "better") || contains(clause, "worse"))
        return Property::COMPARISON;

    if (contains(clause, "when") || contains(clause, "what year") ||
        contains(clause, "what date") || contains(clause, "timeline") ||
        contains(clause, "century") || contains(clause, "era"))
        return Property::TIME;

    if (contains(clause, "advantage") || contains(clause, "benefit") ||
        contains(clause, "strength") || contains(clause, "pro"))
        return Property::ADVANTAGES;

    if (contains(clause, "limitation") || contains(clause, "drawback") ||
        contains(clause, "disadvantage") || contains(clause, "weakness") ||
        contains(clause, "not suitable"))
        return Property::LIMITATIONS;

    if (contains(clause, "used for") || contains(clause, "use case") ||
        contains(clause, "suitable for") || contains(clause, "application") ||
        contains(clause, "start with") || contains(clause, "beginner"))
        return Property::USAGE;

    if (contains(clause, "how does") || contains(clause, "how do") ||
        contains(clause, "purpose") || contains(clause, "function") ||
        contains(clause, "work") || contains(clause, "ensure") ||
        contains(clause, "mechanism"))
        return Property::FUNCTION;

    if (contains(clause, "made of") || contains(clause, "consist") ||
        contains(clause, "compos") || contains(clause, "component") ||
        contains(clause, "type") || contains(clause, "overview") ||
        contains(clause, "part"))
        return Property::COMPOSITION;

    if (contains(clause, "history") || contains(clause, "origin") ||
        contains(clause, "evolv") || contains(clause, "founded") ||
        contains(clause, "heritage") || contains(clause, "why"))
        return Property::HISTORY;

    if (contains(clause, "what is") || contains(clause, "what are") ||
        contains(clause, "define") || contains(clause, "definition") ||
        contains(clause, "meaning") || contains(clause, "refers to"))
        return Property::DEFINITION;

    return Property::GENERAL;
}

AnswerForm RuleBasedQueryAnalyzer::detect_form(const std::string& clause, Property prop) const {
    if (prop == Property::COMPARISON) return AnswerForm::COMPARISON;
    if (prop == Property::COMPOSITION || prop == Property::ADVANTAGES ||
        prop == Property::LIMITATIONS || prop == Property::USAGE)
        return AnswerForm::LIST;
    if (prop == Property::FUNCTION || prop == Property::HISTORY)
        return AnswerForm::EXPLANATION;
    if (prop == Property::LOCATION || prop == Property::TIME)
        return AnswerForm::SHORT_FACT;
    if (contains(clause, "explain") || contains(clause, "describe") ||
        contains(clause, "how") || contains(clause, "why"))
        return AnswerForm::EXPLANATION;
    if (contains(clause, "overview") || contains(clause, "tell me about") ||
        contains(clause, "summarize"))
        return AnswerForm::SUMMARY;
    return AnswerForm::SHORT_FACT;
}

std::vector<InformationNeed> RuleBasedQueryAnalyzer::analyze(const std::string& query) const {
    auto clauses = split_clauses(query);
    std::vector<InformationNeed> needs;
    std::string shared_entity;

    for (auto& clause : clauses) {
        auto kw = extract_keywords(clause);
        auto entity = extract_entity(clause, kw);
        if (entity.empty() && !shared_entity.empty()) entity = shared_entity;
        if (!entity.empty()) shared_entity = entity;

        Property prop = detect_property(clause);
        AnswerForm form = detect_form(clause, prop);
        needs.push_back({entity, prop, form, kw});
    }
    return needs;
}

// Legacy adapter
QueryAnalysis RuleBasedQueryAnalyzer::analyze_legacy(const std::string& query) const {
    auto needs = analyze(query);
    QueryAnalysis qa;
    if (needs.empty()) {
        qa.intent = QueryIntent::GENERAL;
        qa.answerType = AnswerType::SUMMARY;
        return qa;
    }
    auto& n = needs[0];
    qa.mainEntity = n.entity;
    qa.keywords = n.keywords;

    // Map Property → AnswerType
    switch (n.property) {
        case Property::LOCATION:    qa.answerType = AnswerType::LOCATION; break;
        case Property::DEFINITION:  qa.answerType = AnswerType::DEFINITION; break;
        case Property::TIME:        qa.answerType = AnswerType::TEMPORAL; break;
        case Property::COMPARISON:  qa.answerType = AnswerType::COMPARISON; break;
        case Property::FUNCTION:    qa.answerType = AnswerType::PROCEDURE; break;
        case Property::HISTORY:     qa.answerType = AnswerType::SUMMARY; break;
        default:                    qa.answerType = AnswerType::SUMMARY; break;
    }
    // Map AnswerForm → QueryIntent
    switch (n.form) {
        case AnswerForm::SHORT_FACT:  qa.intent = QueryIntent::FACTUAL; break;
        case AnswerForm::EXPLANATION: qa.intent = QueryIntent::EXPLANATION; break;
        case AnswerForm::COMPARISON:  qa.intent = QueryIntent::COMPARISON; break;
        default:                      qa.intent = QueryIntent::GENERAL; break;
    }
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
    (void)model_path; (void)vocab_path;
    return false;
#endif
}

std::vector<InformationNeed> QueryAnalyzer::analyze(const std::string& query) const {
#ifdef HAS_TORCH
    if (use_neural_ && neural_) {
        // Neural produces legacy QueryAnalysis; wrap as single InformationNeed
        auto qa = neural_->analyzer.analyze(query);
        InformationNeed need;
        need.entity = qa.mainEntity;
        need.keywords = qa.keywords;
        switch (qa.answerType) {
            case AnswerType::LOCATION:       need.property = Property::LOCATION; break;
            case AnswerType::DEFINITION:     need.property = Property::DEFINITION; break;
            case AnswerType::TEMPORAL:       need.property = Property::TIME; break;
            case AnswerType::COMPARISON:     need.property = Property::COMPARISON; break;
            case AnswerType::PROCEDURE:      need.property = Property::FUNCTION; break;
            case AnswerType::PERSON_PROFILE: need.property = Property::HISTORY; break;
            default:                         need.property = Property::GENERAL; break;
        }
        switch (qa.answerType) {
            case AnswerType::LOCATION:
            case AnswerType::TEMPORAL:       need.form = AnswerForm::SHORT_FACT; break;
            case AnswerType::COMPARISON:     need.form = AnswerForm::COMPARISON; break;
            case AnswerType::PROCEDURE:      need.form = AnswerForm::EXPLANATION; break;
            default:                         need.form = AnswerForm::SUMMARY; break;
        }
        return {need};
    }
#endif
    return rule_based_.analyze(query);
}

QueryAnalysis QueryAnalyzer::analyze_legacy(const std::string& query) const {
#ifdef HAS_TORCH
    if (use_neural_ && neural_)
        return neural_->analyzer.analyze(query);
#endif
    return rule_based_.analyze_legacy(query);
}
