#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

class Summarizer {
public:
    std::string summarize(
        const std::vector<std::pair<uint32_t, double>>& docs,
        const std::unordered_map<uint32_t, std::string>& doc_text,
        const std::vector<std::string>& query_tokens,
        size_t max_sentences = 5
    );

private:
    std::vector<std::string> split_sentences(const std::string& text);
    double score_sentence(const std::string& sent,
                          const std::vector<std::string>& query_tokens);
};
