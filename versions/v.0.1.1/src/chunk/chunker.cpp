#include "chunker.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>

static std::string to_lower(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return r;
}

static bool has_any(const std::string& text, const std::vector<std::string>& terms) {
    for (auto& t : terms)
        if (text.find(t) != std::string::npos) return true;
    return false;
}

std::vector<std::string> Chunker::split_paragraphs(const std::string& text) const {
    std::vector<std::string> paragraphs;
    std::string current;

    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == '\n') {
            // Double newline or heading = paragraph break
            if (i + 1 < text.size() && (text[i + 1] == '\n' || text[i + 1] == '#')) {
                if (!current.empty()) {
                    // Trim
                    size_t start = current.find_first_not_of(" \t\r\n");
                    if (start != std::string::npos)
                        paragraphs.push_back(current.substr(start));
                    current.clear();
                }
                continue;
            }
        }
        current.push_back(c);
    }

    if (!current.empty()) {
        size_t start = current.find_first_not_of(" \t\r\n");
        if (start != std::string::npos)
            paragraphs.push_back(current.substr(start));
    }

    return paragraphs;
}

ChunkType Chunker::classify_chunk(const std::string& paragraph) const {
    std::string lower = to_lower(paragraph);

    // Location signals
    if (has_any(lower, {"located", "capital", "coast", "island", "city",
                        "region", "country", "bridge", "port", "archipelago",
                        "built across", "connected to"}))
        return ChunkType::LOCATION;

    // Definition signals
    if (has_any(lower, {"is a ", "is an ", "refers to", "defined as",
                        "are step", "are organized", "collection of",
                        "aims to", "studies how"}))
        return ChunkType::DEFINITION;

    // Person signals
    if (has_any(lower, {"born", "inventor", "founded by", "created by",
                        "alan turing", "charles babbage", "ada lovelace",
                        "blaise pascal"}))
        return ChunkType::PERSON;

    // Temporal signals
    if (has_any(lower, {"century", "1642", "1990", "ancient", "early",
                        "modern computing", "history", "evolution",
                        "emerged", "invented"}))
        return ChunkType::TEMPORAL;

    // Procedure signals
    if (has_any(lower, {"step", "how to", "procedure", "process",
                        "algorithm", "method", "technique"}))
        return ChunkType::PROCEDURE;

    // History signals
    if (has_any(lower, {"history", "heritage", "medieval", "centuries",
                        "political", "economic center", "founded"}))
        return ChunkType::HISTORY;

    return ChunkType::GENERAL;
}

std::vector<Chunk> Chunker::chunk_document(uint32_t docId,
                                            const std::string& fullText) const {
    auto paragraphs = split_paragraphs(fullText);
    std::vector<Chunk> chunks;

    for (uint32_t i = 0; i < paragraphs.size(); i++) {
        if (paragraphs[i].size() < 10) continue;

        Chunk c;
        c.docId = docId;
        c.chunkId = i;
        c.type = classify_chunk(paragraphs[i]);
        c.text = paragraphs[i];
        chunks.push_back(std::move(c));
    }

    return chunks;
}

// Property → preferred ChunkTypes mapping
static std::unordered_set<int> preferred_types_for(Property prop) {
    auto s = [](std::initializer_list<ChunkType> ts) {
        std::unordered_set<int> r;
        for (auto t : ts) r.insert(static_cast<int>(t));
        return r;
    };
    switch (prop) {
        case Property::LOCATION:    return s({ChunkType::LOCATION, ChunkType::GENERAL});
        case Property::DEFINITION:  return s({ChunkType::DEFINITION, ChunkType::GENERAL});
        case Property::TIME:        return s({ChunkType::TEMPORAL, ChunkType::HISTORY});
        case Property::HISTORY:     return s({ChunkType::HISTORY, ChunkType::TEMPORAL});
        case Property::FUNCTION:    return s({ChunkType::PROCEDURE, ChunkType::DEFINITION});
        case Property::COMPOSITION: return s({ChunkType::DEFINITION, ChunkType::GENERAL});
        case Property::USAGE:       return s({ChunkType::PROCEDURE, ChunkType::GENERAL});
        case Property::ADVANTAGES:  return s({ChunkType::GENERAL, ChunkType::DEFINITION});
        case Property::LIMITATIONS: return s({ChunkType::GENERAL, ChunkType::DEFINITION});
        default:                    return std::unordered_set<int>{};
    }
}

std::vector<Chunk> Chunker::select_chunks(
    const std::vector<Chunk>& chunks,
    Property property,
    size_t max_chunks)
{
    auto prefs = preferred_types_for(property);

    // Partition: preferred first, then the rest
    std::vector<const Chunk*> preferred, others;
    for (auto& c : chunks) {
        if (prefs.count(static_cast<int>(c.type)))
            preferred.push_back(&c);
        else
            others.push_back(&c);
    }

    std::vector<Chunk> result;
    for (auto* p : preferred) {
        result.push_back(*p);
        if (result.size() >= max_chunks) return result;
    }
    for (auto* p : others) {
        result.push_back(*p);
        if (result.size() >= max_chunks) return result;
    }
    return result;
}
