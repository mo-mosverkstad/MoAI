#pragma once
#include <string>
#include <vector>
#include "../query/information_need.h"

struct IQueryAnalyzer {
    virtual ~IQueryAnalyzer() = default;

    virtual std::vector<InformationNeed>
    analyze(const std::string& query) = 0;

    virtual std::string name() const = 0;
};
