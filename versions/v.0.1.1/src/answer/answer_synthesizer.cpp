#include "answer_synthesizer.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <sstream>
#include <cmath>
#include <regex>

static std::string to_lower(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return r;
}

static bool contains_word(const std::string& text_lower, const std::string& word) {
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

static std::vector<Evidence> filter_by_type(
    const std::vector<Evidence>& evidence,
    const std::vector<ChunkType>& preferred)
{
    std::vector<Evidence> filtered;
    for (auto& e : evidence)
        for (auto t : preferred)
            if (e.type == t) { filtered.push_back(e); break; }
    return filtered.empty() ? evidence : filtered;
}

static void push_segment(std::vector<std::string>& out, const std::string& seg, size_t min_len = 15) {
    size_t start = seg.find_first_not_of(" \t\r\n-");
    if (start != std::string::npos) {
        std::string trimmed = seg.substr(start);
        if (trimmed.size() >= min_len) out.push_back(trimmed);
    }
}

static std::vector<std::string> split_into_segments(const std::string& text, size_t min_len = 15) {
    std::vector<std::string> segments;
    std::istringstream stream(text);
    std::string line, cur;
    while (std::getline(stream, line)) {
        size_t ls = line.find_first_not_of(" \t");
        std::string trimmed = (ls != std::string::npos) ? line.substr(ls) : "";
        if (!trimmed.empty() && (trimmed[0] == '-' || trimmed[0] == '#' || trimmed[0] == '*')) {
            if (!cur.empty()) { push_segment(segments, cur, min_len); cur.clear(); }
            push_segment(segments, trimmed, min_len);
            continue;
        }
        if (trimmed.empty()) {
            if (!cur.empty()) { push_segment(segments, cur, min_len); cur.clear(); }
            continue;
        }
        for (char c : line) {
            cur.push_back(c);
            if (c == '.' || c == '!' || c == '?') {
                push_segment(segments, cur, min_len);
                cur.clear();
            }
        }
        cur.push_back(' ');
    }
    if (!cur.empty()) push_segment(segments, cur, min_len);
    return segments;
}

// Map Property to preferred ChunkTypes
static std::vector<ChunkType> preferred_chunks_for(Property prop) {
    switch (prop) {
        case Property::LOCATION:    return {ChunkType::LOCATION, ChunkType::GENERAL};
        case Property::DEFINITION:  return {ChunkType::DEFINITION, ChunkType::GENERAL};
        case Property::TIME:        return {ChunkType::TEMPORAL, ChunkType::HISTORY};
        case Property::HISTORY:     return {ChunkType::HISTORY, ChunkType::TEMPORAL};
        case Property::FUNCTION:    return {ChunkType::PROCEDURE, ChunkType::DEFINITION};
        case Property::COMPOSITION: return {ChunkType::DEFINITION, ChunkType::GENERAL};
        case Property::USAGE:       return {ChunkType::PROCEDURE, ChunkType::GENERAL};
        default:                    return {};
    }
}

// ── CompositeAnswer ──

std::string CompositeAnswer::merged_text() const {
    std::string result;
    for (auto& p : parts) {
        if (!result.empty()) result += "\n\n";
        result += p.text;
    }
    return result;
}

double CompositeAnswer::overall_confidence() const {
    if (parts.empty()) return 0.0;
    double sum = 0.0;
    for (auto& p : parts) sum += p.confidence;
    return sum / parts.size();
}

// ── Shared methods ──

std::vector<std::string> AnswerSynthesizer::extract_sentences(
    const std::string& text,
    const std::vector<std::string>& keywords,
    size_t max_count) const
{
    auto sentences = split_into_segments(text);
    struct Scored { double score; std::string text; };
    std::vector<Scored> scored;
    for (auto& s : sentences) {
        std::string lower = to_lower(s);
        double sc = 0.0;
        for (auto& k : keywords)
            if (contains_word(lower, k)) sc += 3.0;
        if (s.size() > 30 && s.size() < 300) sc += 0.5;
        if (!s.empty() && s[0] == '#') sc -= 2.0;
        if (sc > 0.0) scored.push_back({sc, s});
    }
    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b) { return a.score > b.score; });
    std::vector<std::string> result;
    for (size_t i = 0; i < max_count && i < scored.size(); i++)
        result.push_back(scored[i].text);
    return result;
}

std::vector<std::string> AnswerSynthesizer::extract_temporal_sentences(
    const std::string& text,
    const std::vector<std::string>& keywords,
    const std::string& entity,
    size_t max_count) const
{
    auto sentences = split_into_segments(text);
    static const std::regex year_re("\\b(1[0-9]{3}|20[0-9]{2})\\b");
    std::string entity_lower = to_lower(entity);
    struct Scored { double score; std::string text; };
    std::vector<Scored> scored;
    for (auto& s : sentences) {
        std::string lower = to_lower(s);
        double sc = 0.0;
        for (auto& k : keywords) {
            if (contains_word(lower, k)) sc += 2.0;
            else {
                std::string stem = (k.size() > 4) ? k.substr(0, k.size() - 2) : k;
                if (lower.find(stem) != std::string::npos) sc += 1.5;
            }
        }
        if (std::regex_search(lower, year_re)) sc += 6.0;
        if (!entity_lower.empty() && contains_word(lower, entity_lower)) sc += 4.0;
        if (s.size() > 30 && s.size() < 300) sc += 0.5;
        if (!s.empty() && s[0] == '#') sc -= 2.0;
        if (sc > 0.0) scored.push_back({sc, s});
    }
    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b) { return a.score > b.score; });
    std::vector<std::string> result;
    for (size_t i = 0; i < max_count && i < scored.size(); i++)
        result.push_back(scored[i].text);
    return result;
}

double AnswerSynthesizer::compute_confidence(
    const std::vector<Evidence>& evidence,
    const std::vector<std::string>& keywords) const
{
    if (evidence.empty() || keywords.empty()) return 0.0;
    std::unordered_set<std::string> found;
    for (auto& e : evidence) {
        std::string lower = to_lower(e.text);
        for (auto& k : keywords)
            if (contains_word(lower, k)) found.insert(k);
    }
    double coverage = static_cast<double>(found.size()) / keywords.size();
    double avg_score = 0.0;
    for (auto& e : evidence) avg_score += e.score;
    avg_score /= evidence.size();
    double relevance = std::min(1.0, avg_score);
    double agreement = std::min(1.0, evidence.size() / 3.0);
    return coverage * 0.4 + relevance * 0.3 + agreement * 0.3;
}

// ── Per-property synthesizers ──

Answer AnswerSynthesizer::synthesize_location(
    const InformationNeed& need, const std::vector<Evidence>& evidence) const
{
    // Score segments with location-specific boosting
    struct Scored { double score; std::string text; };
    std::vector<Scored> all_scored;
    std::string entity_lower = to_lower(need.entity);
    for (auto& e : evidence) {
        auto segs = split_into_segments(e.text);
        for (auto& s : segs) {
            std::string lower = to_lower(s);
            // Hard filter: skip segments that don't mention the entity
            if (!entity_lower.empty() && !contains_word(lower, entity_lower))
                continue;
            double sc = 0.0;
            for (auto& k : need.keywords)
                if (contains_word(lower, k)) sc += 3.0;
            if (!entity_lower.empty()) sc += 4.0; // entity already confirmed above
            // Location-specific language — count each match individually
            static const std::vector<std::string> loc_words = {
                "located", "coast", "eastern", "western", "southern", "northern",
                "situated", "built across", "latitude", "longitude"
            };
            static const std::vector<std::string> loc_nouns = {
                "sea", "ocean", "lake", "river", "island", "peninsula",
                "region", "border", "strait"
            };
            double loc_sc = 0.0;
            for (auto& lw : loc_words)
                if (lower.find(lw) != std::string::npos) loc_sc += 5.0;
            for (auto& ln : loc_nouns)
                if (contains_word(lower, ln)) loc_sc += 3.0;
            // "capital" only counts as location when near entity
            if (contains_word(lower, "capital") && !entity_lower.empty() &&
                contains_word(lower, entity_lower))
                loc_sc += 4.0;
            sc += loc_sc;
            // Penalize non-location content when no location words present
            if (loc_sc == 0.0) {
                if (lower.find("important") != std::string::npos ||
                    lower.find("nobel") != std::string::npos ||
                    lower.find("museum") != std::string::npos ||
                    lower.find("cultural") != std::string::npos ||
                    lower.find("startup") != std::string::npos ||
                    lower.find("tech hub") != std::string::npos ||
                    lower.find("green area") != std::string::npos ||
                    lower.find("nature park") != std::string::npos ||
                    lower.find("ferry") != std::string::npos ||
                    lower.find("airport") != std::string::npos)
                    sc -= 5.0;
            }
            if (s.size() > 30 && s.size() < 300) sc += 0.5;
            if (!s.empty() && s[0] == '#') sc -= 2.0;
            if (sc > 0.0) all_scored.push_back({sc, s});
        }
    }
    std::sort(all_scored.begin(), all_scored.end(),
              [](auto& a, auto& b) { return a.score > b.score; });
    std::string text;
    for (size_t i = 0; i < 3 && i < all_scored.size(); i++) {
        if (!text.empty()) text += " ";
        text += all_scored[i].text;
        if (text.size() > 300) break;
    }
    if (text.empty()) text = "Location information not found.";
    return {text, compute_confidence(evidence, need.keywords), Property::LOCATION};
}

Answer AnswerSynthesizer::synthesize_definition(
    const InformationNeed& need, const std::vector<Evidence>& evidence) const
{
    auto filtered = filter_by_type(evidence, preferred_chunks_for(Property::DEFINITION));
    std::string text;
    for (auto& e : filtered) {
        auto sents = extract_sentences(e.text, need.keywords, 3);
        for (auto& s : sents) { if (!text.empty()) text += " "; text += s; }
        if (text.size() > 500) break;
    }
    if (text.empty()) text = "No definition found.";
    return {text, compute_confidence(filtered, need.keywords), Property::DEFINITION};
}

Answer AnswerSynthesizer::synthesize_temporal(
    const InformationNeed& need, const std::vector<Evidence>& evidence) const
{
    auto filtered = filter_by_type(evidence, preferred_chunks_for(Property::TIME));
    std::string text;
    for (auto& e : filtered) {
        auto sents = extract_temporal_sentences(e.text, need.keywords, need.entity, 3);
        for (auto& s : sents) { if (!text.empty()) text += " "; text += s; }
        if (text.size() > 300) break;
    }
    if (text.empty()) text = "Temporal information not found.";
    return {text, compute_confidence(filtered, need.keywords), Property::TIME};
}

Answer AnswerSynthesizer::synthesize_explanation(
    const InformationNeed& need, const std::vector<Evidence>& evidence) const
{
    auto prefs = preferred_chunks_for(need.property);
    auto filtered = filter_by_type(evidence, prefs);
    std::string entity_lower = to_lower(need.entity);
    std::string text;
    for (auto& e : filtered) {
        auto segs = split_into_segments(e.text);
        for (auto& s : segs) {
            std::string lower = to_lower(s);
            double sc = 0.0;
            for (auto& k : need.keywords)
                if (contains_word(lower, k)) sc += 3.0;
            if (!entity_lower.empty() && contains_word(lower, entity_lower)) sc += 5.0;
            if (sc > 0.0) { if (!text.empty()) text += " "; text += s; }
            if (text.size() > 500) break;
        }
        if (text.size() > 500) break;
    }
    if (text.empty()) text = "No relevant explanation found.";
    return {text, compute_confidence(filtered, need.keywords), need.property};
}

Answer AnswerSynthesizer::synthesize_list(
    const InformationNeed& need, const std::vector<Evidence>& evidence) const
{
    std::string entity_lower = to_lower(need.entity);
    // Score all segments across all evidence by keyword relevance
    struct Scored { double score; std::string text; };
    std::vector<Scored> all_scored;
    for (auto& e : evidence) {
        auto segs = split_into_segments(e.text);
        for (auto& s : segs) {
            std::string lower = to_lower(s);
            double sc = 0.0;
            for (auto& k : need.keywords)
                if (contains_word(lower, k)) sc += 3.0;
            if (!entity_lower.empty() && contains_word(lower, entity_lower)) sc += 4.0;
            if (s.size() > 30 && s.size() < 300) sc += 0.5;
            if (!s.empty() && s[0] == '#') sc -= 2.0;
            if (sc > 0.0) all_scored.push_back({sc, s});
        }
    }
    std::sort(all_scored.begin(), all_scored.end(),
              [](auto& a, auto& b) { return a.score > b.score; });
    std::string text;
    for (size_t i = 0; i < 6 && i < all_scored.size(); i++) {
        if (!text.empty()) text += " ";
        text += all_scored[i].text;
        if (text.size() > 600) break;
    }
    if (text.empty()) text = "No relevant information found.";
    return {text, compute_confidence(evidence, need.keywords), need.property};
}

Answer AnswerSynthesizer::synthesize_general(
    const InformationNeed& need, const std::vector<Evidence>& evidence) const
{
    std::string text;
    for (auto& e : evidence) {
        auto sents = extract_sentences(e.text, need.keywords, 2);
        for (auto& s : sents) { if (!text.empty()) text += " "; text += s; }
        if (text.size() > 600) break;
    }
    if (text.empty()) text = "No relevant information found.";
    return {text, compute_confidence(evidence, need.keywords), need.property};
}

// ── Main dispatch ──

Answer AnswerSynthesizer::synthesize(
    const InformationNeed& need, const std::vector<Evidence>& evidence) const
{
    if (evidence.empty())
        return {"No relevant information found.", 0.0, need.property};

    // Dispatch by property first, then refine by form
    switch (need.property) {
        case Property::LOCATION:
            return synthesize_location(need, evidence);
        case Property::DEFINITION:
            return synthesize_definition(need, evidence);
        case Property::TIME:
            return synthesize_temporal(need, evidence);
        case Property::COMPARISON:
            return synthesize_general(need, evidence); // TODO: dedicated comparator
        default:
            break;
    }

    // Dispatch by answer form
    switch (need.form) {
        case AnswerForm::EXPLANATION:
            return synthesize_explanation(need, evidence);
        case AnswerForm::LIST:
            return synthesize_list(need, evidence);
        case AnswerForm::SHORT_FACT:
            return synthesize_location(need, evidence); // reuse concise extractor
        default:
            return synthesize_general(need, evidence);
    }
}
