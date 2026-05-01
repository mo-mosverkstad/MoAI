#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include "../chunk/chunker.h"
#include "answer_synthesizer.h"

struct NormalizedClaim {
    std::string entity;
    ChunkType property;
    std::unordered_set<std::string> keywords;
    std::unordered_set<std::string> negations;
    uint32_t docId;
};

class EvidenceNormalizer {
public:
    NormalizedClaim normalize(const std::string& entity,
                              const Evidence& ev) const;
};

// Agreement score between two claims (0.0-1.0)
double agreement_score(const NormalizedClaim& a, const NormalizedClaim& b);

// Check if two claims contradict each other
bool contradicts(const NormalizedClaim& a, const NormalizedClaim& b);
