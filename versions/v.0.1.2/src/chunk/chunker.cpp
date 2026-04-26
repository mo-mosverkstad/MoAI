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

    // Location signals — require geographic context, not just "capital"
    if (has_any(lower, {"located", "coast", "archipelago",
                        "built across", "connected to", "situated",
                        "eastern", "western", "southern", "northern",
                        "island nation", "port city"}))
        return ChunkType::LOCATION;
    // "capital" + geographic words = LOCATION; "capital" alone = not enough
    if (has_any(lower, {"capital"}) &&
        has_any(lower, {"island", "city", "region", "country", "bridge",
                        "port", "sea", "lake", "coast", "located"}))
        return ChunkType::LOCATION;

    // Advantages signals
    if (has_any(lower, {"advantage", "benefit", "strength", "why is",
                        "still widely", "widely used", "proven",
                        "mature ecosystem", "important for", "important because"}))
        return ChunkType::ADVANTAGES;

    // Limitations signals
    if (has_any(lower, {"limitation", "drawback", "disadvantage", "weakness",
                        "not suitable", "lack of", "vendor lock"}))
        return ChunkType::LIMITATIONS;

    // Usage signals
    if (has_any(lower, {"used for", "use case", "suitable for", "application",
                        "beginner", "start with", "recommend", "learning path"}))
        return ChunkType::USAGE;

    // Function / mechanism signals
    if (has_any(lower, {"ensures", "mechanism", "how does", "how do",
                        "three-way", "handshake", "retransmit",
                        "flow control", "congestion", "checksum"}))
        return ChunkType::FUNCTION;

    // Definition signals
    if (has_any(lower, {"is a ", "is an ", "refers to", "defined as",
                        "are organized", "collection of",
                        "aims to", "studies how"}))
        return ChunkType::DEFINITION;

    // Person signals
    if (has_any(lower, {"born", "inventor", "founded by", "created by",
                        "alan turing", "charles babbage", "ada lovelace",
                        "blaise pascal"}))
        return ChunkType::PERSON;

    // Temporal signals
    if (has_any(lower, {"century", "1642", "1947", "1990", "ancient", "early",
                        "modern computing", "evolution",
                        "emerged", "invented", "year ", "1960", "1970",
                        "1980", "1989", "1991", "1995", "2000"}))
        return ChunkType::TEMPORAL;

    // Procedure signals
    if (has_any(lower, {"step", "how to", "procedure", "process",
                        "algorithm", "method", "technique"}))
        return ChunkType::PROCEDURE;

    // History signals
    if (has_any(lower, {"history", "heritage", "medieval", "centuries",
                        "political", "economic center", "founded",
                        "origin", "developed", "introduced",
                        "first described", "first practical", "first released"}))
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
        case Property::DEFINITION:  return s({ChunkType::DEFINITION, ChunkType::LOCATION, ChunkType::GENERAL});
        case Property::TIME:        return s({ChunkType::TEMPORAL, ChunkType::HISTORY});
        case Property::HISTORY:     return s({ChunkType::HISTORY, ChunkType::TEMPORAL});
        case Property::FUNCTION:    return s({ChunkType::FUNCTION, ChunkType::PROCEDURE, ChunkType::DEFINITION});
        case Property::COMPOSITION: return s({ChunkType::DEFINITION, ChunkType::GENERAL});
        case Property::USAGE:       return s({ChunkType::USAGE, ChunkType::PROCEDURE, ChunkType::GENERAL});
        case Property::ADVANTAGES:  return s({ChunkType::ADVANTAGES, ChunkType::GENERAL});
        case Property::LIMITATIONS: return s({ChunkType::LIMITATIONS, ChunkType::GENERAL});
        case Property::COMPARISON:  return s({ChunkType::ADVANTAGES, ChunkType::LIMITATIONS, ChunkType::GENERAL});
        default:                    return std::unordered_set<int>{};
    }
}

static bool contains_word_lower(const std::string& text_lower, const std::string& word) {
    size_t pos = 0;
    while ((pos = text_lower.find(word, pos)) != std::string::npos) {
        bool left_ok = (pos == 0) || !std::isalnum(static_cast<unsigned char>(text_lower[pos - 1]));
        bool right_ok = (pos + word.size() >= text_lower.size()) ||
            !std::isalnum(static_cast<unsigned char>(text_lower[pos + word.size()]));
        if (left_ok && right_ok) return true;
        pos++;
    }
    return false;
}

std::vector<Chunk> Chunker::select_chunks(
    const std::vector<Chunk>& chunks,
    Property property,
    const std::vector<std::string>& keywords,
    size_t max_chunks)
{
    // Map property to its primary chunk type (strong match)
    auto primary_type = [](Property p) -> int {
        switch (p) {
            case Property::LOCATION:    return (int)ChunkType::LOCATION;
            case Property::DEFINITION:  return (int)ChunkType::DEFINITION;
            case Property::TIME:        return (int)ChunkType::TEMPORAL;
            case Property::HISTORY:     return (int)ChunkType::HISTORY;
            case Property::FUNCTION:    return (int)ChunkType::FUNCTION;
            case Property::USAGE:       return (int)ChunkType::USAGE;
            case Property::ADVANTAGES:  return (int)ChunkType::ADVANTAGES;
            case Property::LIMITATIONS: return (int)ChunkType::LIMITATIONS;
            default:                    return -1;
        }
    };
    int primary = primary_type(property);
    auto prefs = preferred_types_for(property);

    // Score each chunk by keyword relevance + type preference
    struct Scored { double score; size_t idx; };
    std::vector<Scored> scored;
    // Secondary type: LOCATION chunks are strong for DEFINITION (geographic entities)
    int secondary = (property == Property::DEFINITION) ? (int)ChunkType::LOCATION : -1;
    for (size_t i = 0; i < chunks.size(); i++) {
        auto& c = chunks[i];
        std::string lower = to_lower(c.text);
        double sc = 0.0;
        for (auto& kw : keywords)
            if (contains_word_lower(lower, kw)) sc += 3.0;
        // Strong boost for exact type match, medium for secondary, weak for fallback
        if (static_cast<int>(c.type) == primary) sc += 10.0;
        else if (static_cast<int>(c.type) == secondary) sc += 6.0;
        else if (prefs.count(static_cast<int>(c.type))) sc += 1.0;
        scored.push_back({sc, i});
    }
    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b) { return a.score > b.score; });

    std::vector<Chunk> result;
    for (size_t i = 0; i < max_chunks && i < scored.size(); i++)
        result.push_back(chunks[scored[i].idx]);
    return result;
}
