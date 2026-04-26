#pragma once
#include "../query/information_need.h"

AnswerScope default_scope_for_property(Property p);
AnswerScope adjust_scope_by_confidence(AnswerScope original, double confidence);
size_t max_answer_chars(AnswerScope scope);
size_t max_answer_segments(AnswerScope scope);
