#include "query_analyzer.h"
#include "../answer/answer_scope.h"
#include "../common/config.h"
#include "../common/vocab_loader.h"
#include "../common/rules_loader.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <sstream>

#ifdef HAS_TORCH
#include "neural_query_analyzer.h"
#include <filesystem>
#endif

struct PropertyPrototype {
    Property property;
    std::vector<std::string> signals;
    double weight;
};

struct QueryVocab {
    std::unordered_set<std::string> stop_words;
    std::unordered_set<std::string> non_entity_words;
    std::vector<std::string> clause_split_triggers;
    std::vector<std::string> scope_strict_hints;
    std::vector<std::string> scope_expanded_hints;
    std::vector<std::string> form_explanation_hints;
    std::vector<std::string> form_summary_hints;
    std::vector<PropertyPrototype> prototypes;

    static const QueryVocab& get() {
        static QueryVocab qv = []() {
            auto sw = VocabLoader::load("../config/vocabularies/language.conf");
            auto pr = VocabLoader::load("../config/vocabularies/pipeline_rules.conf");
            auto pp = VocabLoader::load("../config/vocabularies/properties.conf");
            auto& cfg = Config::instance();
            QueryVocab v;
            for (auto& w : VocabLoader::get(sw, "STOP_WORDS")) v.stop_words.insert(w);
            for (auto& w : VocabLoader::get(sw, "NON_ENTITY_WORDS")) v.non_entity_words.insert(w);
            v.clause_split_triggers = VocabLoader::get(pr, "CLAUSE_SPLIT_TRIGGERS");
            v.scope_strict_hints    = VocabLoader::get(pr, "SCOPE_STRICT_HINTS");
            v.scope_expanded_hints  = VocabLoader::get(pr, "SCOPE_EXPANDED_HINTS");
            v.form_explanation_hints = VocabLoader::get(pr, "FORM_EXPLANATION_HINTS");
            v.form_summary_hints    = VocabLoader::get(pr, "FORM_SUMMARY_HINTS");

            // Load property prototypes: words from properties.conf, weights from default.conf
            struct { const char* name; Property prop; } props[] = {
                {"LOCATION", Property::LOCATION}, {"COMPARISON", Property::COMPARISON},
                {"TIME", Property::TIME}, {"ADVANTAGES", Property::ADVANTAGES},
                {"LIMITATIONS", Property::LIMITATIONS}, {"USAGE", Property::USAGE},
                {"FUNCTION", Property::FUNCTION}, {"COMPOSITION", Property::COMPOSITION},
                {"HISTORY", Property::HISTORY}, {"DEFINITION", Property::DEFINITION},
            };
            for (auto& [name, prop] : props) {
                auto& words = VocabLoader::get(pp, std::string("QUERY_") + name);
                double weight = cfg.get_double(std::string("query.weight.") + name, 2.0);
                if (!words.empty())
                    v.prototypes.push_back({prop, words, weight});
            }
            return v;
        }();
        return qv;
    }
};

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
            bool should_split = false;
            for (auto& trigger : QueryVocab::get().clause_split_triggers)
                if (contains(rest, trigger)) { should_split = true; break; }
            if (should_split) {
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
    auto& sw = QueryVocab::get().stop_words;
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
    auto& non_ent = QueryVocab::get().non_entity_words;
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

// Score all properties for a clause — returns sorted (property, score) pairs
static std::vector<std::pair<Property, double>> score_properties(const std::string& clause) {
    std::vector<std::pair<Property, double>> scores;
    for (auto& proto : QueryVocab::get().prototypes) {
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
    // Check config-driven Property -> Form mapping first
    auto& df = PlanningRules::get().default_form;
    auto it = df.find(static_cast<int>(prop));
    if (it != df.end()) return static_cast<AnswerForm>(it->second);

    // Fall back to query wording hints
    for (auto& h : QueryVocab::get().form_explanation_hints)
        if (contains(clause, h)) return AnswerForm::EXPLANATION;
    for (auto& h : QueryVocab::get().form_summary_hints)
        if (contains(clause, h)) return AnswerForm::SUMMARY;
    return AnswerForm::SHORT_FACT;
}

AnswerScope RuleBasedQueryAnalyzer::infer_scope(const std::string& clause, AnswerForm form) const {
    auto& qv = QueryVocab::get();
    for (auto& h : qv.scope_strict_hints)
        if (contains(clause, h)) return AnswerScope::STRICT;
    for (auto& h : qv.scope_expanded_hints)
        if (contains(clause, h)) return AnswerScope::EXPANDED;

    // 2. Form-based defaults (no query wording hint)
    //    Property-based defaults are applied later in the pipeline
    //    via default_scope_for_property() — here we only handle
    //    form-level signals that aren't already covered by property.
    return AnswerScope::NORMAL; // placeholder; overridden by property default
}

std::vector<InformationNeed> RuleBasedQueryAnalyzer::analyze(const std::string& query) {
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
QueryAnalysis RuleBasedQueryAnalyzer::analyze_legacy(const std::string& query) {
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

