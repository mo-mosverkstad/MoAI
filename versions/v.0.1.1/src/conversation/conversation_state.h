#pragma once
#include <string>
#include <vector>
#include "../query/information_need.h"

class ConversationState {
public:
    void update(const InformationNeed& need);
    void apply(std::vector<InformationNeed>& needs) const;
    void reset();

    bool has_context() const { return !lastEntity_.empty(); }
    const std::string& last_entity() const { return lastEntity_; }

private:
    std::string lastEntity_;
    Property lastProperty_ = Property::GENERAL;
};
