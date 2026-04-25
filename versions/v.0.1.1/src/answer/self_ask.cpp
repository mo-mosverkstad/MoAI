#include "self_ask.h"
#include <cctype>

static std::string to_lower_sa(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return r;
}

// Self-ask rules (deterministic):
//   ADVANTAGES  → needs DEFINITION + LOCATION
//   COMPARISON  → needs DEFINITION of each entity
//   LIMITATIONS → needs USAGE context
//   HISTORY     → needs TEMPORAL + DEFINITION
//   FUNCTION    → needs DEFINITION
std::vector<InformationNeed> SelfAskModule::expand(const InformationNeed& need) const {
    std::vector<InformationNeed> derived;
    std::string entity = need.entity;

    auto make = [&](Property p, AnswerForm f, std::vector<std::string> kw) {
        InformationNeed n;
        n.entity = entity;
        n.property = p;
        n.form = f;
        n.keywords = kw;
        n.keywords.push_back(entity);
        return n;
    };

    switch (need.property) {
        case Property::ADVANTAGES:
            derived.push_back(make(Property::DEFINITION, AnswerForm::SHORT_FACT,
                {"definition"}));
            derived.push_back(make(Property::LOCATION, AnswerForm::SHORT_FACT,
                {"located", "region"}));
            break;
        case Property::COMPARISON:
            derived.push_back(make(Property::DEFINITION, AnswerForm::SHORT_FACT,
                {"definition"}));
            break;
        case Property::LIMITATIONS:
            derived.push_back(make(Property::USAGE, AnswerForm::EXPLANATION,
                {"used for", "application"}));
            break;
        case Property::HISTORY:
            derived.push_back(make(Property::TIME, AnswerForm::SHORT_FACT,
                {"year", "century"}));
            derived.push_back(make(Property::DEFINITION, AnswerForm::SHORT_FACT,
                {"definition"}));
            break;
        case Property::FUNCTION:
            derived.push_back(make(Property::DEFINITION, AnswerForm::SHORT_FACT,
                {"definition"}));
            break;
        default:
            break;
    }
    return derived;
}

double SelfAskModule::check_support_coverage(
    const std::vector<InformationNeed>& support_needs,
    const std::vector<std::string>& evidence_texts) const
{
    if (support_needs.empty()) return 1.0;

    std::string combined;
    for (auto& t : evidence_texts) {
        combined += " ";
        combined += to_lower_sa(t);
    }

    int total = 0, found = 0;
    for (auto& sn : support_needs) {
        for (auto& kw : sn.keywords) {
            total++;
            if (combined.find(to_lower_sa(kw)) != std::string::npos)
                found++;
        }
    }
    return total > 0 ? static_cast<double>(found) / total : 1.0;
}
