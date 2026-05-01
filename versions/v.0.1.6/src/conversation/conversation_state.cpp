#include "conversation_state.h"
#include <fstream>

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

bool ConversationState::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << lastEntity_ << "\n" << static_cast<int>(lastProperty_) << "\n";
    return f.good();
}

bool ConversationState::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::getline(f, lastEntity_);
    int prop = 0;
    if (f >> prop)
        lastProperty_ = static_cast<Property>(prop);
    return true;
}
