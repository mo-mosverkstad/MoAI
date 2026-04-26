#include "answer_compressor.h"
#include "../common/config.h"
#include <vector>

struct CompressorConfig {
    double min_conf, min_agr;
    size_t strong_ev_count;
    double strong_ev_conf, ns_conf, ns_agr, el_conf, el_agr;

    static const CompressorConfig& get() {
        static CompressorConfig cc = []() {
            auto& c = Config::instance();
            return CompressorConfig{
                c.get_double("compression.min_confidence", 0.6),
                c.get_double("compression.min_agreement", 0.6),
                c.get_size("compression.strong_evidence_count", 4),
                c.get_double("compression.strong_evidence_confidence", 0.9),
                c.get_double("compression.normal_strong_confidence", 0.85),
                c.get_double("compression.normal_strong_agreement", 0.7),
                c.get_double("compression.expanded_light_confidence", 0.85),
                c.get_double("compression.expanded_light_agreement", 0.7),
            };
        }();
        return cc;
    }
};

static std::vector<std::string> split_sentences(const std::string& text) {
    std::vector<std::string> sents;
    std::string cur;
    for (size_t i = 0; i < text.size(); i++) {
        cur.push_back(text[i]);
        if ((text[i] == '.' || text[i] == '!' || text[i] == '?') &&
            (i + 1 >= text.size() || text[i + 1] == ' ' || text[i + 1] == '\n')) {
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
    auto& cc = CompressorConfig::get();
    if (ctx.confidence < cc.min_conf) return CompressionLevel::NONE;
    if (ctx.agreement < cc.min_agr)   return CompressionLevel::NONE;
    if (ctx.scope == AnswerScope::STRICT) return CompressionLevel::NONE;

    if (ctx.evidence_count >= cc.strong_ev_count && ctx.confidence >= cc.strong_ev_conf)
        return CompressionLevel::STRONG;
    if (ctx.scope == AnswerScope::NORMAL &&
        ctx.confidence >= cc.ns_conf && ctx.agreement >= cc.ns_agr)
        return CompressionLevel::STRONG;
    if (ctx.scope == AnswerScope::EXPANDED &&
        ctx.confidence >= cc.el_conf && ctx.agreement >= cc.el_agr)
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
