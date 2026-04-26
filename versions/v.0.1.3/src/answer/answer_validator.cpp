#include "answer_validator.h"
#include "evidence_normalizer.h"
#include "../common/config.h"
#include "../common/vocab_loader.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <unordered_map>
#include <cmath>

struct ValidatorConfig {
    double fail_ratio, weak_ratio, fail_mult, weak_mult;
    double max_penalty, per_pair;
    double w_cov, w_vol, w_agr, w_pen, vol_div;
    std::unordered_map<int, std::vector<std::string>> signals;

    static const ValidatorConfig& get() {
        static ValidatorConfig vc = []() {
            auto& c = Config::instance();
            ValidatorConfig v;
            v.fail_ratio = c.get_double("validator.signal_ratio_fail", 0.0);
            v.weak_ratio = c.get_double("validator.signal_ratio_weak", 0.15);
            v.fail_mult  = c.get_double("validator.fail_confidence_multiplier", 0.3);
            v.weak_mult  = c.get_double("validator.weak_confidence_multiplier", 0.7);
            v.max_penalty = c.get_double("confidence.max_contradiction_penalty", 0.4);
            v.per_pair    = c.get_double("confidence.contradiction_penalty_per_pair", 0.15);
            v.w_cov = c.get_double("confidence.coverage_weight", 0.3);
            v.w_vol = c.get_double("confidence.volume_weight", 0.2);
            v.w_agr = c.get_double("confidence.agreement_weight", 0.3);
            v.w_pen = c.get_double("confidence.penalty_weight", 0.2);
            v.vol_div = c.get_double("confidence.volume_divisor", 3.0);

            // Load signal words from vocabulary file
            auto m = VocabLoader::load("../config/vocabularies/validator_signals.conf");
            v.signals[(int)Property::LOCATION]    = VocabLoader::get(m, "LOCATION");
            v.signals[(int)Property::DEFINITION]  = VocabLoader::get(m, "DEFINITION");
            v.signals[(int)Property::FUNCTION]    = VocabLoader::get(m, "FUNCTION");
            v.signals[(int)Property::ADVANTAGES]  = VocabLoader::get(m, "ADVANTAGES");
            v.signals[(int)Property::LIMITATIONS] = VocabLoader::get(m, "LIMITATIONS");
            v.signals[(int)Property::USAGE]       = VocabLoader::get(m, "USAGE");
            v.signals[(int)Property::HISTORY]     = VocabLoader::get(m, "HISTORY");
            v.signals[(int)Property::TIME]        = VocabLoader::get(m, "TIME");
            v.signals[(int)Property::COMPARISON]  = VocabLoader::get(m, "COMPARISON");
            return v;
        }();
        return vc;
    }
};

static std::string to_lower_v(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return r;
}


void AnswerValidator::validate(Answer& answer, const InformationNeed& need) const {
    auto& vc = ValidatorConfig::get();

    if (answer.text.empty() || answer.text.find("not found") != std::string::npos) {
        answer.validated = false;
        answer.validation_note = "No relevant content found for this property.";
        answer.confidence = std::min(answer.confidence, 0.1);
        return;
    }

    auto it = vc.signals.find((int)need.property);
    static const std::vector<std::string> empty;
    auto& signals = (it != vc.signals.end()) ? it->second : empty;
    if (signals.empty()) { answer.validated = true; return; }

    std::string lower = to_lower_v(answer.text);
    int matches = 0;
    for (auto& sig : signals)
        if (lower.find(sig) != std::string::npos) matches++;

    double signal_ratio = static_cast<double>(matches) / signals.size();

    if (signal_ratio <= vc.fail_ratio) {
        answer.validated = false;
        answer.validation_note = "Answer does not appear to address " +
            std::string(property_str(need.property)) + " (0 signal words found).";
        answer.confidence *= vc.fail_mult;
    } else if (signal_ratio < vc.weak_ratio) {
        answer.validated = true;
        answer.validation_note = "Weak property match (" +
            std::to_string(matches) + "/" + std::to_string(signals.size()) + " signals).";
        answer.confidence *= vc.weak_mult;
    } else {
        answer.validated = true;
    }
}

EvidenceAnalysis AnswerValidator::analyze_evidence(
    const std::vector<Evidence>& evidence,
    const std::string& entity) const
{
    auto& vc = ValidatorConfig::get();
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
    result.confidence_penalty = std::min(vc.max_penalty, result.contradiction_pairs * vc.per_pair);
    return result;
}

double AnswerValidator::compute_refined_confidence(
    const std::vector<Evidence>& evidence,
    const std::string& entity,
    const std::vector<std::string>& keywords) const
{
    if (evidence.empty() || keywords.empty()) return 0.0;
    auto& vc = ValidatorConfig::get();

    std::unordered_set<std::string> found;
    for (auto& e : evidence) {
        std::string lower = to_lower_v(e.text);
        for (auto& k : keywords)
            if (lower.find(to_lower_v(k)) != std::string::npos) found.insert(k);
    }
    double coverage = static_cast<double>(found.size()) / keywords.size();
    double volume = std::min(1.0, evidence.size() / vc.vol_div);
    auto analysis = analyze_evidence(evidence, entity);

    double confidence = vc.w_cov * coverage + vc.w_vol * volume +
        vc.w_agr * analysis.agreement + vc.w_pen * (1.0 - analysis.confidence_penalty);
    return std::max(0.0, std::min(1.0, confidence));
}
