#pragma once
#include <vector>
#include <utility>
#include "../query/information_need.h"

struct QuestionPlan {
    std::vector<InformationNeed> needs;
    std::vector<std::pair<int, int>> dependencies; // (prerequisite, dependent)
};

class QuestionPlanner {
public:
    QuestionPlan build(const std::vector<InformationNeed>& needs) const;

private:
    // Explicit dependency rules: does need `a` depend on need `b`?
    // i.e., must `b` be answered before `a`?
    bool depends_on(const InformationNeed& a, const InformationNeed& b) const;
};
