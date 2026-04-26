#pragma once
#include "../query/information_need.h"

// Adjust scope based on confidence: high confidence → compress, low → expand
inline AnswerScope adjust_scope_by_confidence(AnswerScope original, double confidence) {
    if (confidence > 0.85 && original == AnswerScope::NORMAL)
        return AnswerScope::STRICT;
    if (confidence < 0.5 && original == AnswerScope::STRICT)
        return AnswerScope::NORMAL;
    return original;
}

// Max characters for answer text based on scope
inline size_t max_answer_chars(AnswerScope scope) {
    switch (scope) {
        case AnswerScope::STRICT:   return 200;
        case AnswerScope::NORMAL:   return 400;
        case AnswerScope::EXPANDED: return 700;
    }
    return 400;
}

// Max segments/sentences to include based on scope
inline size_t max_answer_segments(AnswerScope scope) {
    switch (scope) {
        case AnswerScope::STRICT:   return 2;
        case AnswerScope::NORMAL:   return 4;
        case AnswerScope::EXPANDED: return 8;
    }
    return 4;
}
