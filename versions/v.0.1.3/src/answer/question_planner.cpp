#include "question_planner.h"
#include <algorithm>

// Explicit dependency rules:
// "a depends on b" means b must be answered before a
bool QuestionPlanner::depends_on(const InformationNeed& a,
                                 const InformationNeed& b) const {
    // HISTORY depends on LOCATION (know where before what happened there)
    if (a.property == Property::HISTORY && b.property == Property::LOCATION)
        return true;
    // ADVANTAGES depends on LOCATION (importance depends on context)
    if (a.property == Property::ADVANTAGES && b.property == Property::LOCATION)
        return true;
    // ADVANTAGES depends on DEFINITION (know what it is before why it matters)
    if (a.property == Property::ADVANTAGES && b.property == Property::DEFINITION)
        return true;
    // FUNCTION depends on DEFINITION (know what it is before how it works)
    if (a.property == Property::FUNCTION && b.property == Property::DEFINITION)
        return true;
    // LIMITATIONS depends on DEFINITION
    if (a.property == Property::LIMITATIONS && b.property == Property::DEFINITION)
        return true;
    // COMPARISON depends on DEFINITION
    if (a.property == Property::COMPARISON && b.property == Property::DEFINITION)
        return true;
    // USAGE depends on DEFINITION
    if (a.property == Property::USAGE && b.property == Property::DEFINITION)
        return true;
    return false;
}

QuestionPlan QuestionPlanner::build(const std::vector<InformationNeed>& needs) const {
    QuestionPlan plan;
    const int n = static_cast<int>(needs.size());
    if (n == 0) return plan;

    // Build dependency edges
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (i != j && depends_on(needs[i], needs[j]))
                plan.dependencies.emplace_back(j, i); // j before i

    // Topological sort (n is small, brute force)
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
                break; // one per iteration for stable ordering
            }
        }
    }
    // Add any remaining (shouldn't happen unless cycles)
    for (int i = 0; i < n; i++)
        if (!placed[i]) plan.needs.push_back(needs[i]);

    return plan;
}
