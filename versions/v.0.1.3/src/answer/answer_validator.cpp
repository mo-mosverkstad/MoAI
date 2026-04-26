#include "answer_validator.h"
#include "evidence_normalizer.h"
#include "../common/config.h"
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

static const std::vector<std::string>& expected_signals(Property prop) {
    static const std::unordered_map<int, std::vector<std::string>> m = {
        {(int)Property::LOCATION,    {"located", "capital", "coast", "city", "region",
                                      "sea", "island", "eastern", "western", "southern",
                                      "northern", "situated", "built"}},
        {(int)Property::DEFINITION,  {"is a", "is an", "refers to", "defined", "means",
                                      "collection", "system", "organized",
                                      "capital", "largest", "known for", "known as"}},
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

void AnswerValidator::validate(Answer& answer, const InformationNeed& need) const {
    auto& c = Config::instance();
    double fail_ratio = c.get_double("validator.signal_ratio_fail", 0.0);
    double weak_ratio = c.get_double("validator.signal_ratio_weak", 0.15);
    double fail_mult  = c.get_double("validator.fail_confidence_multiplier", 0.3);
    double weak_mult  = c.get_double("validator.weak_confidence_multiplier", 0.7);

    if (answer.text.empty() || answer.text.find("not found") != std::string::npos) {
        answer.validated = false;
        answer.validation_note = "No relevant content found for this property.";
        answer.confidence = std::min(answer.confidence, 0.1);
        return;
    }

    std::string lower = to_lower_v(answer.text);
    auto& signals = expected_signals(need.property);
    if (signals.empty()) { answer.validated = true; return; }

    int matches = 0;
    for (auto& sig : signals)
        if (lower.find(sig) != std::string::npos) matches++;

    double signal_ratio = static_cast<double>(matches) / signals.size();

    if (signal_ratio <= fail_ratio) {
        answer.validated = false;
        answer.validation_note = "Answer does not appear to address " +
            std::string(property_str(need.property)) + " (0 signal words found).";
        answer.confidence *= fail_mult;
    } else if (signal_ratio < weak_ratio) {
        answer.validated = true;
        answer.validation_note = "Weak property match (" +
            std::to_string(matches) + "/" + std::to_string(signals.size()) + " signals).";
        answer.confidence *= weak_mult;
    } else {
        answer.validated = true;
    }
}

EvidenceAnalysis AnswerValidator::analyze_evidence(
    const std::vector<Evidence>& evidence,
    const std::string& entity) const
{
    auto& c = Config::instance();
    double max_penalty = c.get_double("confidence.max_contradiction_penalty", 0.4);
    double per_pair    = c.get_double("confidence.contradiction_penalty_per_pair", 0.15);

    EvidenceAnalysis result = {0.0, 0, 0, 0.0};
    if (evidence.size() < 2) { result.agreement = 0.5; return result; }

    EvidenceNormalizer normalizer;
    std::vector<NormalizedClaim> claims;
    for (auto& e : evidence)
        claims.push_back(normalizer.normalize(entity, e));

    double agreement_sum = 0.0;
    for (size_t i = 0; i < claims.size(); i++) {
        for (size_t j = i + 1; j < claims.size(); j++) {
            if (contradicts(claims[i], claims[j])) {
                result.contradiction_pairs++;
            } else {
                double a = agreement_score(claims[i], claims[j]);
                if (a > 0.0) { agreement_sum += a; result.agreement_pairs++; }
            }
        }
    }

    result.agreement = result.agreement_pairs > 0
        ? agreement_sum / result.agreement_pairs : 0.5;
    result.confidence_penalty = std::min(max_penalty, result.contradiction_pairs * per_pair);
    return result;
}

double AnswerValidator::compute_refined_confidence(
    const std::vector<Evidence>& evidence,
    const std::string& entity,
    const std::vector<std::string>& keywords) const
{
    if (evidence.empty() || keywords.empty()) return 0.0;
    auto& c = Config::instance();
    double w_cov = c.get_double("confidence.coverage_weight", 0.3);
    double w_vol = c.get_double("confidence.volume_weight", 0.2);
    double w_agr = c.get_double("confidence.agreement_weight", 0.3);
    double w_pen = c.get_double("confidence.penalty_weight", 0.2);
    double vol_div = c.get_double("confidence.volume_divisor", 3.0);

    std::unordered_set<std::string> found;
    for (auto& e : evidence) {
        std::string lower = to_lower_v(e.text);
        for (auto& k : keywords)
            if (lower.find(to_lower_v(k)) != std::string::npos) found.insert(k);
    }
    double coverage = static_cast<double>(found.size()) / keywords.size();
    double volume = std::min(1.0, evidence.size() / vol_div);
    auto analysis = analyze_evidence(evidence, entity);

    double confidence = w_cov * coverage + w_vol * volume +
        w_agr * analysis.agreement + w_pen * (1.0 - analysis.confidence_penalty);
    return std::max(0.0, std::min(1.0, confidence));
}
