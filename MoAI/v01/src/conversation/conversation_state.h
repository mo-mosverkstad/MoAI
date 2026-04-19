#pragma once
#include <string>
#include "../query/query_analyzer.h"

class ConversationState {
public:
    void update(const std::string& entity, AnswerType answerType);
    void apply(QueryAnalysis& analysis) const;
    void reset();

    bool has_context() const { return !lastEntity_.empty(); }
    const std::string& last_entity() const { return lastEntity_; }

private:
    std::string lastEntity_;
    AnswerType lastAnswerType_ = AnswerType::SUMMARY;
};
