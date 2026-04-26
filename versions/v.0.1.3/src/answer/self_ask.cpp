#include "self_ask.h"
#include "../common/rules_loader.h"
#include <cctype>

static std::string to_lower_sa(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return r;
}

std::vector<InformationNeed> SelfAskModule::expand(const InformationNeed& need) const {
    std::vector<InformationNeed> derived;
    auto& rules = PlanningRules::get();

    for (auto& rule : rules.self_ask) {
        if (rule.trigger != need.property) continue;
        InformationNeed n;
        n.entity = need.entity;
        n.property = rule.sub_property;
        n.form = rule.form;
        n.keywords = rule.keywords;
        n.keywords.push_back(need.entity);
        derived.push_back(n);
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
