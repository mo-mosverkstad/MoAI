#include "chunker.h"
#include "../common/config.h"
#include "../common/vocab_loader.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>

struct ChunkConfig {
    size_t min_chunk_size;
    double kw_boost, pri_boost, sec_boost, fb_boost;

    static const ChunkConfig& get() {
        static ChunkConfig cc = []() {
            auto& c = Config::instance();
            return ChunkConfig{
                c.get_size("chunk.min_chunk_size", 10),
                c.get_double("chunk.keyword_boost", 3.0),
                c.get_double("chunk.primary_type_boost", 10.0),
                c.get_double("chunk.secondary_type_boost", 6.0),
                c.get_double("chunk.fallback_type_boost", 1.0),
            };
        }();
        return cc;
    }
};

struct ChunkVocab {
    std::vector<std::string> location, loc_cap_ctx, loc_cap_req;
    std::vector<std::string> advantages, limitations, usage, function;
    std::vector<std::string> definition, person, temporal, procedure, history;

    static const ChunkVocab& get() {
        static ChunkVocab cv = []() {
            auto m = VocabLoader::load("../config/vocabularies/chunk_signals.conf");
            return ChunkVocab{
                VocabLoader::get(m, "LOCATION"),
                VocabLoader::get(m, "LOCATION_CAPITAL_CONTEXT"),
                VocabLoader::get(m, "LOCATION_CAPITAL_REQUIRES"),
                VocabLoader::get(m, "ADVANTAGES"),
                VocabLoader::get(m, "LIMITATIONS"),
                VocabLoader::get(m, "USAGE"),
                VocabLoader::get(m, "FUNCTION"),
                VocabLoader::get(m, "DEFINITION"),
                VocabLoader::get(m, "PERSON"),
                VocabLoader::get(m, "TEMPORAL"),
                VocabLoader::get(m, "PROCEDURE"),
                VocabLoader::get(m, "HISTORY"),
            };
        }();
        return cv;
    }
};

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
    auto& v = ChunkVocab::get();

    if (has_any(lower, v.location)) return ChunkType::LOCATION;
    if (has_any(lower, v.loc_cap_ctx) && has_any(lower, v.loc_cap_req))
        return ChunkType::LOCATION;
    if (has_any(lower, v.advantages))  return ChunkType::ADVANTAGES;
    if (has_any(lower, v.limitations)) return ChunkType::LIMITATIONS;
    if (has_any(lower, v.usage))       return ChunkType::USAGE;
    if (has_any(lower, v.function))    return ChunkType::FUNCTION;
    if (has_any(lower, v.definition))  return ChunkType::DEFINITION;
    if (has_any(lower, v.person))      return ChunkType::PERSON;
    if (has_any(lower, v.temporal))    return ChunkType::TEMPORAL;
    if (has_any(lower, v.procedure))   return ChunkType::PROCEDURE;
    if (has_any(lower, v.history))     return ChunkType::HISTORY;
    return ChunkType::GENERAL;
}

std::vector<Chunk> Chunker::chunk_document(uint32_t docId,
                                            const std::string& fullText) const {
    auto paragraphs = split_paragraphs(fullText);
    std::vector<Chunk> chunks;

    for (uint32_t i = 0; i < paragraphs.size(); i++) {
        if (paragraphs[i].size() < ChunkConfig::get().min_chunk_size) continue;

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

    auto& cc = ChunkConfig::get();

    // Score each chunk by keyword relevance + type preference
    struct Scored { double score; size_t idx; };
    std::vector<Scored> scored;
    int secondary = (property == Property::DEFINITION) ? (int)ChunkType::LOCATION : -1;
    for (size_t i = 0; i < chunks.size(); i++) {
        auto& c = chunks[i];
        std::string lower = to_lower(c.text);
        double sc = 0.0;
        for (auto& kw : keywords)
            if (contains_word_lower(lower, kw)) sc += cc.kw_boost;
        if (static_cast<int>(c.type) == primary) sc += cc.pri_boost;
        else if (static_cast<int>(c.type) == secondary) sc += cc.sec_boost;
        else if (prefs.count(static_cast<int>(c.type))) sc += cc.fb_boost;
        scored.push_back({sc, i});
    }
    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b) { return a.score > b.score; });

    std::vector<Chunk> result;
    for (size_t i = 0; i < max_chunks && i < scored.size(); i++)
        result.push_back(chunks[scored[i].idx]);
    return result;
}
