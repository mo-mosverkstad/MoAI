#include "chunker.h"
#include "../common/config.h"
#include "../common/vocab_loader.h"
#include "../common/rules_loader.h"
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
    // Ordered list of types to check (first match wins)
    std::vector<std::string> types;
    // Type name -> signal words
    std::unordered_map<std::string, std::vector<std::string>> signals;
    // Special: LOCATION_CAPITAL requires both context and requires lists
    std::vector<std::string> loc_cap_ctx, loc_cap_req;

    static const ChunkVocab& get() {
        static ChunkVocab cv = []() {
            auto m = VocabLoader::load("../config/vocabularies/chunk_signals.conf");
            ChunkVocab v;
            v.types = VocabLoader::get(m, "TYPES");
            for (auto& t : v.types)
                v.signals[t] = VocabLoader::get(m, t);
            v.loc_cap_ctx = VocabLoader::get(m, "LOCATION_CAPITAL_CONTEXT");
            v.loc_cap_req = VocabLoader::get(m, "LOCATION_CAPITAL_REQUIRES");
            return v;
        }();
        return cv;
    }
};

static ChunkType name_to_chunk_type(const std::string& name) {
    static const std::unordered_map<std::string, ChunkType> m = {
        {"LOCATION", ChunkType::LOCATION}, {"DEFINITION", ChunkType::DEFINITION},
        {"FUNCTION", ChunkType::FUNCTION}, {"USAGE", ChunkType::USAGE},
        {"HISTORY", ChunkType::HISTORY}, {"TEMPORAL", ChunkType::TEMPORAL},
        {"ADVANTAGES", ChunkType::ADVANTAGES}, {"LIMITATIONS", ChunkType::LIMITATIONS},
        {"PERSON", ChunkType::PERSON}, {"PROCEDURE", ChunkType::PROCEDURE},
    };
    auto it = m.find(name);
    return (it != m.end()) ? it->second : ChunkType::GENERAL;
}

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

    for (auto& type_name : v.types) {
        // Special handling: LOCATION also checks capital+context
        if (type_name == "LOCATION") {
            auto it = v.signals.find("LOCATION");
            if (it != v.signals.end() && has_any(lower, it->second))
                return ChunkType::LOCATION;
            if (has_any(lower, v.loc_cap_ctx) && has_any(lower, v.loc_cap_req))
                return ChunkType::LOCATION;
            continue;
        }
        auto it = v.signals.find(type_name);
        if (it != v.signals.end() && has_any(lower, it->second))
            return name_to_chunk_type(type_name);
    }
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
    auto& pc = PlanningRules::get().preferred_chunks;
    auto it = pc.find(static_cast<int>(prop));
    if (it == pc.end()) return {};
    return std::unordered_set<int>(it->second.begin(), it->second.end());
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
