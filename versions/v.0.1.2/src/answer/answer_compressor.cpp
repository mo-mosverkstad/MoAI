#include "answer_compressor.h"
#include <vector>

static std::vector<std::string> split_sentences(const std::string& text) {
    std::vector<std::string> sents;
    std::string cur;
    for (size_t i = 0; i < text.size(); i++) {
        cur.push_back(text[i]);
        if ((text[i] == '.' || text[i] == '!' || text[i] == '?') &&
            (i + 1 >= text.size() || text[i + 1] == ' ' || text[i + 1] == '\n')) {
            // Trim leading whitespace
            size_t s = cur.find_first_not_of(" \t\r\n");
            if (s != std::string::npos && cur.size() - s > 10)
                sents.push_back(cur.substr(s));
            cur.clear();
        }
    }
    if (!cur.empty()) {
        size_t s = cur.find_first_not_of(" \t\r\n");
        if (s != std::string::npos && cur.size() - s > 10)
            sents.push_back(cur.substr(s));
    }
    return sents;
}

CompressionLevel AgreementCompressor::decide_level(const CompressionContext& ctx) const {
    if (ctx.confidence < 0.6)  return CompressionLevel::NONE;
    if (ctx.agreement < 0.6)   return CompressionLevel::NONE;
    if (ctx.scope == AnswerScope::STRICT) return CompressionLevel::NONE;

    if (ctx.evidence_count >= 4 && ctx.confidence >= 0.9)
        return CompressionLevel::STRONG;

    if (ctx.scope == AnswerScope::NORMAL &&
        ctx.confidence >= 0.85 && ctx.agreement >= 0.7)
        return CompressionLevel::STRONG;

    if (ctx.scope == AnswerScope::EXPANDED &&
        ctx.confidence >= 0.85 && ctx.agreement >= 0.7)
        return CompressionLevel::LIGHT;

    return CompressionLevel::NONE;
}

std::string AgreementCompressor::compress_light(const std::string& text) const {
    auto sents = split_sentences(text);
    if (sents.size() <= 2) return text;
    return sents[0] + " " + sents[1];
}

std::string AgreementCompressor::compress_strong(const std::string& text) const {
    auto sents = split_sentences(text);
    if (sents.empty()) return text;
    return sents[0];
}

std::string AgreementCompressor::compress(const std::string& answer_text,
                                           const CompressionContext& ctx) const {
    switch (decide_level(ctx)) {
        case CompressionLevel::LIGHT:  return compress_light(answer_text);
        case CompressionLevel::STRONG: return compress_strong(answer_text);
        default:                       return answer_text;
    }
}
