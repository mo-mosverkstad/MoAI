#include "question_planner.h"
#include "../common/rules_loader.h"
#include <algorithm>

bool QuestionPlanner::depends_on(const InformationNeed& a,
                                 const InformationNeed& b) const {
    auto& rules = PlanningRules::get();
    for (auto& dep : rules.dependencies)
        if (a.property == dep.property && b.property == dep.depends_on)
            return true;
    return false;
}

QuestionPlan QuestionPlanner::build(const std::vector<InformationNeed>& needs) const {
    QuestionPlan plan;
    const int n = static_cast<int>(needs.size());
    if (n == 0) return plan;

    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (i != j && depends_on(needs[i], needs[j]))
                plan.dependencies.emplace_back(j, i);

    std::vector<bool> placed(n, false);
    for (int iter = 0; iter < n; iter++) {
        for (int i = 0; i < n; i++) {
            if (placed[i]) continue;
            bool blocked = false;
            for (auto& [pre, dep] : plan.dependencies)
                if (dep == i && !placed[pre]) { blocked = true; break; }
            if (!blocked) {
                plan.needs.push_back(needs[i]);
                placed[i] = true;
                break;
            }
        }
    }
    for (int i = 0; i < n; i++)
        if (!placed[i]) plan.needs.push_back(needs[i]);

    return plan;
}
