#pragma once
#include <string>
#include <vector>

// What aspect of the entity is being asked about
enum class Property {
    LOCATION,
    DEFINITION,
    FUNCTION,
    COMPOSITION,
    HISTORY,
    TIME,
    COMPARISON,
    ADVANTAGES,
    LIMITATIONS,
    USAGE,
    GENERAL
};

// How the answer should be structured
enum class AnswerForm {
    SHORT_FACT,
    EXPLANATION,
    LIST,
    COMPARISON,
    SUMMARY
};

// How much information to produce (orthogonal to Property)
enum class AnswerScope {
    STRICT,     // Minimal factual answer (1-2 sentences)
    NORMAL,     // Default: fact + 1 supporting context (2-4 sentences)
    EXPANDED    // Explainer / tutorial-style (multi-paragraph)
};

struct InformationNeed {
    std::string entity;
    Property property;
    AnswerForm form;
    AnswerScope scope = AnswerScope::NORMAL;
    std::vector<std::string> keywords;
    double property_score = 1.0;
    bool is_support = false;
};

inline const char* property_str(Property p) {
    switch (p) {
        case Property::LOCATION:    return "LOCATION";
        case Property::DEFINITION:  return "DEFINITION";
        case Property::FUNCTION:    return "FUNCTION";
        case Property::COMPOSITION: return "COMPOSITION";
        case Property::HISTORY:     return "HISTORY";
        case Property::TIME:        return "TIME";
        case Property::COMPARISON:  return "COMPARISON";
        case Property::ADVANTAGES:  return "ADVANTAGES";
        case Property::LIMITATIONS: return "LIMITATIONS";
        case Property::USAGE:       return "USAGE";
        case Property::GENERAL:     return "GENERAL";
    }
    return "UNKNOWN";
}

inline const char* answer_form_str(AnswerForm f) {
    switch (f) {
        case AnswerForm::SHORT_FACT:  return "SHORT_FACT";
        case AnswerForm::EXPLANATION: return "EXPLANATION";
        case AnswerForm::LIST:        return "LIST";
        case AnswerForm::COMPARISON:  return "COMPARISON";
        case AnswerForm::SUMMARY:     return "SUMMARY";
    }
    return "UNKNOWN";
}

inline const char* scope_str(AnswerScope s) {
    switch (s) {
        case AnswerScope::STRICT:   return "STRICT";
        case AnswerScope::NORMAL:   return "NORMAL";
        case AnswerScope::EXPANDED: return "EXPANDED";
    }
    return "UNKNOWN";
}
