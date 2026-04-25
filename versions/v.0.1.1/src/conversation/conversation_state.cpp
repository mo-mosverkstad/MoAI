#include "conversation_state.h"

void ConversationState::update(const InformationNeed& need) {
    if (!need.entity.empty()) lastEntity_ = need.entity;
    lastProperty_ = need.property;
}

void ConversationState::apply(std::vector<InformationNeed>& needs) const {
    for (auto& n : needs) {
        if (n.entity.empty() && !lastEntity_.empty()) {
            n.entity = lastEntity_;
            n.keywords.push_back(lastEntity_);
        }
    }
}

void ConversationState::reset() {
    lastEntity_.clear();
    lastProperty_ = Property::GENERAL;
}
