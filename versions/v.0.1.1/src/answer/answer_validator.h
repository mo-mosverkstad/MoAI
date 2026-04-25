#pragma once
#include <string>
#include <vector>
#include "../query/information_need.h"
#include "answer_synthesizer.h"

class AnswerValidator {
public:
    // 6.2: Self-ask — does the answer actually address the property?
    // Returns adjusted confidence and sets validation_note
    void validate(Answer& answer, const InformationNeed& need) const;

    // 6.3: Conflict detection — do evidence chunks contradict each other?
    // Returns conflict penalty (0.0 = no conflict, up to 0.3 = strong conflict)
    double detect_conflicts(const std::vector<Evidence>& evidence,
                            const std::string& entity) const;
};
