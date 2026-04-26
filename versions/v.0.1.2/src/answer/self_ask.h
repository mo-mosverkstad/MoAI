#pragma once
#include <vector>
#include <string>
#include "../query/information_need.h"

class SelfAskModule {
public:
    // Expand a need into support sub-needs that should be answered first.
    // These get added to the plan and answered by the pipeline.
    std::vector<InformationNeed> expand(const InformationNeed& need) const;

    // Check if evidence covers the support needs (returns coverage 0.0-1.0)
    double check_support_coverage(
        const std::vector<InformationNeed>& support_needs,
        const std::vector<std::string>& evidence_texts) const;
};
