#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "../query/information_need.h"
#include "../chunk/chunker.h"
#include "../query/query_analyzer.h"

struct SelfAskRule {
    Property trigger;
    Property sub_property;
    AnswerForm form;
    std::vector<std::string> keywords;
};

struct DependencyRule {
    Property property;
    Property depends_on;
};

struct QueryTemplate {
    std::string prefix;
    std::string suffix;
    QueryIntent intent;
    AnswerType answer_type;
};

struct PlanningRules {
    std::vector<SelfAskRule> self_ask;
    std::vector<DependencyRule> dependencies;
    std::unordered_map<int, std::vector<int>> preferred_chunks; // Property -> ChunkType list
    std::vector<QueryTemplate> query_templates;
    std::unordered_map<int, int> default_form; // Property -> AnswerForm

    static const PlanningRules& get();
};
