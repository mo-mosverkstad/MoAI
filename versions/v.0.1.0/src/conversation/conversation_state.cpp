#include "conversation_state.h"

void ConversationState::update(const std::string& entity, AnswerType answerType) {
    if (!entity.empty()) lastEntity_ = entity;
    lastAnswerType_ = answerType;
}

void ConversationState::apply(QueryAnalysis& analysis) const {
    // If query has no entity, reuse last entity
    if (analysis.mainEntity.empty() && !lastEntity_.empty()) {
        analysis.mainEntity = lastEntity_;
        analysis.keywords.push_back(lastEntity_);
    }
}

void ConversationState::reset() {
    lastEntity_.clear();
    lastAnswerType_ = AnswerType::SUMMARY;
}
