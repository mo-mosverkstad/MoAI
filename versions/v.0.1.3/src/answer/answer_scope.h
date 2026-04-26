#pragma once
#include "../query/information_need.h"
#include "../common/config.h"

inline AnswerScope default_scope_for_property(Property p) {
    switch (p) {
        case Property::LOCATION:
        case Property::DEFINITION:
        case Property::TIME:
            return AnswerScope::STRICT;
        case Property::FUNCTION:
        case Property::USAGE:
        case Property::ADVANTAGES:
        case Property::LIMITATIONS:
            return AnswerScope::NORMAL;
        case Property::HISTORY:
        case Property::COMPARISON:
            return AnswerScope::EXPANDED;
        default:
            return AnswerScope::NORMAL;
    }
}

inline AnswerScope adjust_scope_by_confidence(AnswerScope original, double confidence) {
    auto& c = Config::instance();
    double hi = c.get_double("scope.high_confidence_threshold", 0.85);
    double lo = c.get_double("scope.low_confidence_threshold", 0.5);
    if (confidence > hi && original == AnswerScope::NORMAL)
        return AnswerScope::STRICT;
    if (confidence < lo && original == AnswerScope::STRICT)
        return AnswerScope::NORMAL;
    return original;
}

inline size_t max_answer_chars(AnswerScope scope) {
    auto& c = Config::instance();
    switch (scope) {
        case AnswerScope::STRICT:   return c.get_size("scope.strict_max_chars", 200);
        case AnswerScope::NORMAL:   return c.get_size("scope.normal_max_chars", 400);
        case AnswerScope::EXPANDED: return c.get_size("scope.expanded_max_chars", 700);
    }
    return 400;
}

inline size_t max_answer_segments(AnswerScope scope) {
    auto& c = Config::instance();
    switch (scope) {
        case AnswerScope::STRICT:   return c.get_size("scope.strict_max_segments", 2);
        case AnswerScope::NORMAL:   return c.get_size("scope.normal_max_segments", 4);
        case AnswerScope::EXPANDED: return c.get_size("scope.expanded_max_segments", 8);
    }
    return 4;
}
