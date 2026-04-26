#include "query_analyzer.h"
#include "../answer/answer_scope.h"
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

// Words that describe properties but are not entities
static const std::unordered_set<std::string>& non_entity_words() {
    static const std::unordered_set<std::string> w = {
        "important", "famous", "popular", "useful", "good", "bad",
        "better", "worse", "best", "worst", "great", "large", "small",
        "scalable", "reliable", "fast", "slow", "cheap", "expensive",
        "suitable", "mainstream", "widely", "still", "connected",
        "limitation", "limitations", "advantage", "advantages",
        "benefit", "benefits", "drawback", "drawbacks",
        "overview", "difference", "comparison", "beginner", "beginners"
    };
    return w;
}

std::string RuleBasedQueryAnalyzer::extract_entity(
    const std::string& clause, const std::vector<std::string>& keywords) const
{
    if (keywords.empty()) return "";
    auto& non_ent = non_entity_words();
    // Prefer keywords that are not generic adjectives/property words
    std::string best;
    for (auto& k : keywords) {
        if (non_ent.count(k)) continue;
        if (k.size() > best.size()) best = k;
    }
    // If all keywords are non-entity words, return empty to trigger
    // shared entity propagation from previous clauses
    return best;
}

// ── Semantic prototypes: small vocabularies per property ──

struct PropertyPrototype {
    Property property;
    std::vector<std::string> signals;
    double weight;  // base weight per signal match
};

static const std::vector<PropertyPrototype>& property_prototypes() {
    static const std::vector<PropertyPrototype> protos = {
        {Property::LOCATION,    {"locat", "where", "capital", "coast", "close to", "near",
                                 "region", "country", " city", "geograph", "situated",
                                 "border", "sea", "lake", "island"}, 3.0},
        {Property::COMPARISON,  {"vs ", "versus", "compare", "difference between",
                                 "better", "worse", "which", "better than"}, 3.0},
        {Property::TIME,        {"when", "what year", "what date", "timeline",
                                 "century", "era", "year", "period", "date"}, 3.0},
        {Property::ADVANTAGES,  {"advantage", "benefit", "strength", "pro ",
                                 "why is", "why are", "still widely", "widely used",
                                 "important", "famous", "significant"}, 2.5},
        {Property::LIMITATIONS, {"limitation", "drawback", "disadvantage", "weakness",
                                 "not suitable", "problem with", "problem of",
                                 "challenge", "problem"}, 2.5},
        {Property::USAGE,       {"used for", "use case", "suitable for", "application",
                                 "start with", "beginner", "recommend", "suitable"}, 2.5},
        {Property::FUNCTION,    {"how does", "how do", "purpose", "function",
                                 "work", "ensure", "mechanism", "process"}, 2.0},
        {Property::COMPOSITION, {"made of", "consist", "compos", "component",
                                 "type", "overview", "part of"}, 2.0},
        {Property::HISTORY,     {"history", "origin", "evolv", "founded",
                                 "heritage", "developed", "introduced"}, 1.5},
        {Property::DEFINITION,  {"what is", "what are", "define", "definition",
                                 "meaning", "refers to", "means"}, 1.5},
    };
    return protos;
}

// Score all properties for a clause — returns sorted (property, score) pairs
static std::vector<std::pair<Property, double>> score_properties(const std::string& clause) {
    std::vector<std::pair<Property, double>> scores;
    for (auto& proto : property_prototypes()) {
        double sc = 0.0;
        for (auto& signal : proto.signals)
            if (contains(clause, signal)) sc += proto.weight;
        if (sc > 0.0) scores.push_back({proto.property, sc});
    }
    std::sort(scores.begin(), scores.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    return scores;
}

Property RuleBasedQueryAnalyzer::detect_property(const std::string& clause) const {
    auto scores = score_properties(clause);
    return scores.empty() ? Property::GENERAL : scores[0].first;
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

AnswerScope RuleBasedQueryAnalyzer::infer_scope(const std::string& clause, AnswerForm form) const {
    // 1. Explicit scope hints from query wording (always win)
    if (contains(clause, "brief") || contains(clause, "short") ||
        contains(clause, "quick") || contains(clause, "just"))
        return AnswerScope::STRICT;

    if (contains(clause, "in detail") || contains(clause, "explain") ||
        contains(clause, "overview") || contains(clause, "describe") ||
        contains(clause, "comprehensive") || contains(clause, "thorough"))
        return AnswerScope::EXPANDED;

    // 2. Form-based defaults (no query wording hint)
    //    Property-based defaults are applied later in the pipeline
    //    via default_scope_for_property() — here we only handle
    //    form-level signals that aren't already covered by property.
    return AnswerScope::NORMAL; // placeholder; overridden by property default
}

std::vector<InformationNeed> RuleBasedQueryAnalyzer::analyze(const std::string& query) const {
    auto clauses = split_clauses(query);
    std::vector<InformationNeed> needs;
    std::string shared_entity;

    for (auto& clause : clauses) {
        auto kw = extract_keywords(clause);
        auto entity = extract_entity(clause, kw);
        if (entity.empty() && !shared_entity.empty()) {
            entity = shared_entity;
            // Inject shared entity into keywords so retrieval finds it
            bool has_entity = false;
            for (auto& k : kw)
                if (k == entity) { has_entity = true; break; }
            if (!has_entity) kw.insert(kw.begin(), entity);
        }
        if (!entity.empty()) shared_entity = entity;

        Property prop = detect_property(clause);
        AnswerForm form = detect_form(clause, prop);
        AnswerScope scope = infer_scope(clause, form);
        // If no explicit query hint, use property-based default
        if (scope == AnswerScope::NORMAL)
            scope = default_scope_for_property(prop);

        // Store property confidence from scoring
        auto pscores = score_properties(clause);
        double pscore = pscores.empty() ? 0.0 : pscores[0].second;

        InformationNeed need;
        need.entity = entity;
        need.property = prop;
        need.form = form;
        need.scope = scope;
        need.keywords = kw;
        need.property_score = pscore;
        needs.push_back(std::move(need));
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
