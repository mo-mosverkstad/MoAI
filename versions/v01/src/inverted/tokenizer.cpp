#include "tokenizer.h"
#include <cctype>

std::vector<std::string> Tokenizer::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    std::string cur;

    for (unsigned char c : text) {
        if (std::isalnum(c)) {
            cur.push_back(std::tolower(c));
        } else {
            if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
            }
        }
    }
    if (!cur.empty()) tokens.push_back(cur);

    return tokens;
}