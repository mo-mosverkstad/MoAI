#pragma once
#include <string>
#include "answer_scope.h"

enum class CompressionLevel { NONE, LIGHT, STRONG };

struct CompressionContext {
    AnswerScope scope;
    double confidence;
    double agreement;
    size_t evidence_count;
};

class AgreementCompressor {
public:
    std::string compress(const std::string& answer_text,
                         const CompressionContext& ctx) const;
    CompressionLevel decide_level(const CompressionContext& ctx) const;

private:
    std::string compress_light(const std::string& text) const;
    std::string compress_strong(const std::string& text) const;
};

inline const char* compression_str(CompressionLevel l) {
    switch (l) {
        case CompressionLevel::NONE:   return "NONE";
        case CompressionLevel::LIGHT:  return "LIGHT";
        case CompressionLevel::STRONG: return "STRONG";
    }
    return "NONE";
}
