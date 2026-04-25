#include "answer_validator.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <unordered_map>
#include <cmath>

static std::string to_lower_v(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return r;
}

static bool has_any_v(const std::string& text, const std::vector<std::string>& terms) {
    for (auto& t : terms)
        if (text.find(t) != std::string::npos) return true;
    return false;
}

// Property → expected signal words that should appear in a valid answer
static const std::vector<std::string>& expected_signals(Property prop) {
    static const std::unordered_map<int, std::vector<std::string>> m = {
        {(int)Property::LOCATION,    {"located", "capital", "coast", "city", "region",
                                      "sea", "island", "eastern", "western", "southern",
                                      "northern", "situated", "built"}},
        {(int)Property::DEFINITION,  {"is a", "is an", "refers to", "defined", "means",
                                      "collection", "system", "organized"}},
        {(int)Property::FUNCTION,    {"ensures", "mechanism", "works", "provides",
                                      "handles", "protocol", "process", "control"}},
        {(int)Property::ADVANTAGES,  {"advantage", "benefit", "strength", "widely",
                                      "proven", "reliable", "powerful", "mature",
                                      "important", "significant", "leading", "major"}},
        {(int)Property::LIMITATIONS, {"limitation", "drawback", "disadvantage", "lack",
                                      "not suitable", "weaker", "costly", "vendor"}},
        {(int)Property::USAGE,       {"used for", "use case", "beginner", "start with",
                                      "recommend", "suitable", "learning"}},
        {(int)Property::HISTORY,     {"history", "founded", "century", "origin",
                                      "developed", "introduced", "heritage"}},
        {(int)Property::TIME,        {"year", "century", "date", "period", "invented",
                                      "introduced", "created"}},
        {(int)Property::COMPARISON,  {"vs", "compare", "difference", "better", "worse",
                                      "more", "less", "affordable"}},
    };
    auto it = m.find((int)prop);
    static const std::vector<std::string> empty;
    return it != m.end() ? it->second : empty;
}

// 6.2: Self-ask validation
void AnswerValidator::validate(Answer& answer, const InformationNeed& need) const {
    if (answer.text.empty() || answer.text.find("not found") != std::string::npos) {
        answer.validated = false;
        answer.validation_note = "No relevant content found for this property.";
        answer.confidence = std::min(answer.confidence, 0.1);
        return;
    }

    std::string lower = to_lower_v(answer.text);
    auto& signals = expected_signals(need.property);

    if (signals.empty()) {
        answer.validated = true;
        return;
    }

    // Count how many expected signals appear in the answer
    int matches = 0;
    for (auto& sig : signals)
        if (lower.find(sig) != std::string::npos) matches++;

    double signal_ratio = static_cast<double>(matches) / signals.size();

    if (signal_ratio == 0.0) {
        // Answer contains zero property-relevant signals
        answer.validated = false;
        answer.validation_note = "Answer does not appear to address " +
            std::string(property_str(need.property)) + " (0 signal words found).";
        answer.confidence *= 0.3;
    } else if (signal_ratio < 0.15) {
        answer.validated = true;
        answer.validation_note = "Weak property match (" +
            std::to_string(matches) + "/" + std::to_string(signals.size()) +
            " signals).";
        answer.confidence *= 0.7;
    } else {
        answer.validated = true;
    }
}

// 6.3: Conflict detection
// Looks for contradicting factual claims across evidence chunks
double AnswerValidator::detect_conflicts(
    const std::vector<Evidence>& evidence,
    const std::string& entity) const
{
    if (evidence.size() < 2) return 0.0;

    std::string entity_lower = to_lower_v(entity);

    // Extract key factual fragments from each evidence chunk
    // A "fact" is a short phrase containing the entity + a descriptor
    struct Fact {
        std::string text;
        uint32_t docId;
    };
    std::vector<Fact> facts;

    // Negation words that indicate contradiction
    static const std::vector<std::string> negations = {
        "not ", "no ", "never ", "neither ", "without ", "lack"
    };

    for (auto& e : evidence) {
        std::string lower = to_lower_v(e.text);
        if (!entity_lower.empty() && lower.find(entity_lower) == std::string::npos)
            continue;

        // Extract sentences containing the entity
        std::string cur;
        for (char c : e.text) {
            cur.push_back(c);
            if (c == '.' || c == '!' || c == '?') {
                std::string sl = to_lower_v(cur);
                if (sl.find(entity_lower) != std::string::npos && cur.size() > 20)
                    facts.push_back({sl, e.docId});
                cur.clear();
            }
        }
    }

    if (facts.size() < 2) return 0.0;

    // Check for contradictions: same entity but one has negation, other doesn't
    int conflicts = 0;
    int comparisons = 0;
    for (size_t i = 0; i < facts.size(); i++) {
        for (size_t j = i + 1; j < facts.size(); j++) {
            if (facts[i].docId == facts[j].docId) continue;
            comparisons++;

            bool i_neg = has_any_v(facts[i].text, negations);
            bool j_neg = has_any_v(facts[j].text, negations);
            if (i_neg != j_neg) conflicts++;
        }
    }

    if (comparisons == 0) return 0.0;
    double conflict_ratio = static_cast<double>(conflicts) / comparisons;
    return std::min(0.3, conflict_ratio * 0.5);
}
