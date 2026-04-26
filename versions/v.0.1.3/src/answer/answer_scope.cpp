#include "answer_scope.h"
#include "../common/config.h"

struct ScopeConfig {
    double hi_conf, lo_conf;
    size_t strict_chars, normal_chars, expanded_chars;
    size_t strict_segs, normal_segs, expanded_segs;

    static const ScopeConfig& get() {
        static ScopeConfig sc = []() {
            auto& c = Config::instance();
            return ScopeConfig{
                c.get_double("scope.high_confidence_threshold", 0.85),
                c.get_double("scope.low_confidence_threshold", 0.5),
                c.get_size("scope.strict_max_chars", 200),
                c.get_size("scope.normal_max_chars", 400),
                c.get_size("scope.expanded_max_chars", 700),
                c.get_size("scope.strict_max_segments", 2),
                c.get_size("scope.normal_max_segments", 4),
                c.get_size("scope.expanded_max_segments", 8),
            };
        }();
        return sc;
    }
};

AnswerScope default_scope_for_property(Property p) {
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

AnswerScope adjust_scope_by_confidence(AnswerScope original, double confidence) {
    auto& sc = ScopeConfig::get();
    if (confidence > sc.hi_conf && original == AnswerScope::NORMAL)
        return AnswerScope::STRICT;
    if (confidence < sc.lo_conf && original == AnswerScope::STRICT)
        return AnswerScope::NORMAL;
    return original;
}

size_t max_answer_chars(AnswerScope scope) {
    auto& sc = ScopeConfig::get();
    switch (scope) {
        case AnswerScope::STRICT:   return sc.strict_chars;
        case AnswerScope::NORMAL:   return sc.normal_chars;
        case AnswerScope::EXPANDED: return sc.expanded_chars;
    }
    return sc.normal_chars;
}

size_t max_answer_segments(AnswerScope scope) {
    auto& sc = ScopeConfig::get();
    switch (scope) {
        case AnswerScope::STRICT:   return sc.strict_segs;
        case AnswerScope::NORMAL:   return sc.normal_segs;
        case AnswerScope::EXPANDED: return sc.expanded_segs;
    }
    return sc.normal_segs;
}
