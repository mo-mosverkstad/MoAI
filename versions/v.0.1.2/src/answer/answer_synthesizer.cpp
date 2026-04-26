#include "answer_synthesizer.h"
#include "answer_scope.h"
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
        case Property::FUNCTION:    return {ChunkType::FUNCTION, ChunkType::PROCEDURE, ChunkType::DEFINITION};
        case Property::COMPOSITION: return {ChunkType::DEFINITION, ChunkType::GENERAL};
        case Property::USAGE:       return {ChunkType::USAGE, ChunkType::PROCEDURE, ChunkType::GENERAL};
        case Property::ADVANTAGES:  return {ChunkType::ADVANTAGES, ChunkType::GENERAL};
        case Property::LIMITATIONS: return {ChunkType::LIMITATIONS, ChunkType::GENERAL};
        case Property::COMPARISON:  return {ChunkType::ADVANTAGES, ChunkType::LIMITATIONS, ChunkType::GENERAL};
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
    std::string entity_lower = to_lower(need.entity);

    // First: prefer LOCATION-typed chunks that are ABOUT the entity
    // Scan all evidence, pick the best LOCATION chunk by entity proximity
    const Evidence* best_loc = nullptr;
    for (auto& e : evidence) {
        if (e.type == ChunkType::LOCATION && e.text.size() > 50) {
            std::string lower = to_lower(e.text);
            if (!entity_lower.empty()) {
                auto pos = lower.find(entity_lower);
                if (pos == std::string::npos || pos > 100) continue;
            }
            // Prefer chunks from the entity's own document (entity in first sentence)
            if (!best_loc) {
                best_loc = &e;
            } else {
                // Pick the one with more location words
                std::string bl = to_lower(best_loc->text);
                int cur_loc = 0, new_loc = 0;
                for (auto& w : {"located", "coast", "eastern", "situated", "sea", "lake", "island"})
                    { if (lower.find(w) != std::string::npos) new_loc++;
                      if (bl.find(w) != std::string::npos) cur_loc++; }
                if (new_loc > cur_loc) best_loc = &e;
            }
        }
    }
    if (best_loc) {
        std::string text = best_loc->text;
        if (text.size() > 400) text.resize(400);
        return {text, compute_confidence(evidence, need.keywords), Property::LOCATION};
    }

    // Fallback: score segments with location-specific boosting
    struct Scored { double score; std::string text; };
    std::vector<Scored> all_scored;
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
    std::string entity_lower = to_lower(need.entity);

    // First: prefer DEFINITION-typed chunks with entity in opening "X is" pattern
    const Evidence* best_def = nullptr;
    double best_def_score = 0.0;
    for (auto& e : evidence) {
        if (e.type != ChunkType::DEFINITION && e.type != ChunkType::LOCATION) continue;
        if (e.text.size() < 30) continue;
        std::string lower = to_lower(e.text);
        if (!entity_lower.empty()) {
            auto pos = lower.find(entity_lower);
            if (pos == std::string::npos || pos > 30) continue;
        }
        double sc = 0.0;
        // Strong boost for "entity is" at the start
        std::string is_pat = entity_lower + " is ";
        if (!entity_lower.empty() && lower.find(is_pat) < 50) sc += 10.0;
        for (auto& dp : {"is a ", "is an ", "is the ", "capital", "known for",
                         "refers to", "defined as"})
            if (lower.find(dp) != std::string::npos) sc += 3.0;
        // Penalize non-definitional content
        for (auto& np : {"expensive", "rent ", "cost", "salary", "startup",
                         "tech hub", "ranking", "ranks", "ecosystem",
                         "important for", "important because"})
            if (lower.find(np) != std::string::npos) sc -= 4.0;
        if (sc > best_def_score) { best_def = &e; best_def_score = sc; }
    }
    if (best_def && best_def_score > 5.0) {
        std::string text = best_def->text;
        if (text.size() > 500) text.resize(500);
        return {text, compute_confidence(evidence, need.keywords), Property::DEFINITION};
    }

    // Fallback: score all segments, boosting definitional language
    struct Scored { double score; std::string text; };
    std::vector<Scored> all_scored;
    for (auto& e : evidence) {
        auto segs = split_into_segments(e.text);
        for (auto& s : segs) {
            if (s.size() < 20) continue;
            if (!s.empty() && s[0] == '#') continue;
            std::string lower = to_lower(s);
            double sc = 0.0;
            for (auto& k : need.keywords)
                if (contains_word(lower, k)) sc += 3.0;
            if (!entity_lower.empty() && contains_word(lower, entity_lower)) sc += 4.0;
            // Definitional patterns
            for (auto& dp : {"is a ", "is an ", "is the ", "refers to",
                             "defined as", "known for", "known as",
                             "collection of", "organized"})
                if (lower.find(dp) != std::string::npos) sc += 5.0;
            // Entity as subject: "X is/are ..." pattern
            if (!entity_lower.empty()) {
                auto epos = lower.find(entity_lower);
                if (epos != std::string::npos && epos < 30) {
                    sc += 6.0; // entity is the subject
                    if (lower.find(entity_lower + " is ") != std::string::npos ||
                        lower.find(entity_lower + " are ") != std::string::npos)
                        sc += 8.0;
                }
            }
            // Penalize non-definitional content
            for (auto& np : {"expensive", "rent ", "cost", "salary",
                             "startup", "tech hub", "ranking", "ranks"})
                if (lower.find(np) != std::string::npos) sc -= 3.0;
            if (sc > 0.0) all_scored.push_back({sc, s});
        }
    }
    std::sort(all_scored.begin(), all_scored.end(),
              [](auto& a, auto& b) { return a.score > b.score; });
    std::string text;
    for (size_t i = 0; i < 4 && i < all_scored.size(); i++) {
        if (!text.empty()) text += " ";
        text += all_scored[i].text;
        if (text.size() > 500) break;
    }
    if (text.empty()) text = "No definition found.";
    return {text, compute_confidence(evidence, need.keywords), Property::DEFINITION};
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

// ── Typed synthesizers: ADVANTAGES, LIMITATIONS, USAGE, HISTORY, COMPARISON ──

static std::vector<std::string> scored_segments(
    const std::vector<Evidence>& evidence,
    const std::vector<std::string>& keywords,
    const std::string& entity_lower,
    const std::vector<std::string>& boost_words,
    size_t max_segs, size_t max_chars)
{
    struct Scored { double score; std::string text; };
    std::vector<Scored> all;
    for (auto& e : evidence) {
        auto segs = split_into_segments(e.text);
        for (auto& s : segs) {
            // Skip headings and very short fragments
            if (!s.empty() && s[0] == '#') continue;
            if (s.size() < 30) continue;
            std::string lower = to_lower(s);
            double sc = 0.0;
            for (auto& k : keywords)
                if (contains_word(lower, k)) sc += 3.0;
            if (!entity_lower.empty() && contains_word(lower, entity_lower)) sc += 4.0;
            for (auto& bw : boost_words)
                if (lower.find(bw) != std::string::npos) sc += 2.0;
            // Prefer substantive sentences over bare labels
            if (s.size() > 60) sc += 1.0;
            if (sc > 0.0) all.push_back({sc, s});
        }
    }
    std::sort(all.begin(), all.end(),
              [](auto& a, auto& b) { return a.score > b.score; });
    std::vector<std::string> result;
    size_t chars = 0;
    for (size_t i = 0; i < max_segs && i < all.size(); i++) {
        result.push_back(all[i].text);
        chars += all[i].text.size();
        if (chars > max_chars) break;
    }
    return result;
}

Answer AnswerSynthesizer::synthesize_advantages(
    const InformationNeed& need, const std::vector<Evidence>& evidence) const
{
    std::string entity_lower = to_lower(need.entity);
    // First: prefer ADVANTAGES-typed chunks about the entity
    for (auto& e : evidence) {
        if (e.type == ChunkType::ADVANTAGES && e.text.size() > 50) {
            std::string lower = to_lower(e.text);
            if (!entity_lower.empty()) {
                auto pos = lower.find(entity_lower);
                if (pos == std::string::npos || pos > 50) continue;
            }
            std::string text = e.text;
            if (text.size() > 700) text.resize(700);
            return {text, compute_confidence(evidence, need.keywords), Property::ADVANTAGES};
        }
    }

    // Fallback: score segments, but filter to entity-mentioning segments
    auto segs = scored_segments(evidence, need.keywords, entity_lower,
        {"advantage", "benefit", "strength", "widely", "proven", "mature",
         "standardiz", "reliable", "powerful", "important", "significant",
         "leading", "major", "innovation"}, 6, 600);
    // Filter to segments primarily about the entity (entity in first 50 chars)
    std::string text;
    for (auto& s : segs) {
        if (!entity_lower.empty()) {
            std::string sl = to_lower(s);
            auto pos = sl.find(entity_lower);
            if (pos == std::string::npos || pos > 50) continue;
        }
        if (!text.empty()) text += " ";
        text += s;
        if (text.size() > 600) break;
    }
    // If no entity-specific segments, use all
    if (text.empty()) {
        for (auto& s : segs) {
            if (!text.empty()) text += " ";
            text += s;
            if (text.size() > 600) break;
        }
    }
    if (text.empty()) text = "No advantages information found.";
    return {text, compute_confidence(evidence, need.keywords), Property::ADVANTAGES};
}

Answer AnswerSynthesizer::synthesize_limitations(
    const InformationNeed& need, const std::vector<Evidence>& evidence) const
{
    // First: prefer LIMITATIONS-typed chunks directly
    for (auto& e : evidence) {
        if (e.type == ChunkType::LIMITATIONS && e.text.size() > 50) {
            std::string text = e.text;
            if (text.size() > 700) text.resize(700);
            return {text, compute_confidence(evidence, need.keywords), Property::LIMITATIONS};
        }
    }
    // Fallback: score segments
    auto segs = scored_segments(evidence, need.keywords, to_lower(need.entity),
        {"limitation", "drawback", "disadvantage", "lack", "not suitable",
         "weaker", "less mature", "vendor lock", "costly"}, 6, 600);
    std::string text;
    for (auto& s : segs) { if (!text.empty()) text += " "; text += s; }
    if (text.empty()) text = "No limitations information found.";
    return {text, compute_confidence(evidence, need.keywords), Property::LIMITATIONS};
}

Answer AnswerSynthesizer::synthesize_usage(
    const InformationNeed& need, const std::vector<Evidence>& evidence) const
{
    for (auto& e : evidence) {
        if (e.type == ChunkType::USAGE && e.text.size() > 50) {
            std::string text = e.text;
            if (text.size() > 700) text.resize(700);
            return {text, compute_confidence(evidence, need.keywords), Property::USAGE};
        }
    }
    auto segs = scored_segments(evidence, need.keywords, to_lower(need.entity),
        {"used for", "use case", "beginner", "start with", "recommend",
         "suitable", "learning path", "best for"}, 6, 600);
    std::string text;
    for (auto& s : segs) { if (!text.empty()) text += " "; text += s; }
    if (text.empty()) text = "No usage information found.";
    return {text, compute_confidence(evidence, need.keywords), Property::USAGE};
}

Answer AnswerSynthesizer::synthesize_history(
    const InformationNeed& need, const std::vector<Evidence>& evidence) const
{
    std::string entity_lower = to_lower(need.entity);
    // First: prefer HISTORY-typed chunks about the entity (best by signal count)
    const Evidence* best_hist = nullptr;
    int best_signals = 0;
    for (auto& e : evidence) {
        if (e.type == ChunkType::HISTORY && e.text.size() > 50) {
            std::string lower = to_lower(e.text);
            if (!entity_lower.empty()) {
                auto pos = lower.find(entity_lower);
                if (pos == std::string::npos || pos > 200) continue;
            }
            int sig = 0;
            for (auto& w : {"history", "founded", "century", "centuries",
                            "origin", "heritage", "medieval", "political"})
                if (lower.find(w) != std::string::npos) sig++;
            if (sig > best_signals) { best_hist = &e; best_signals = sig; }
        }
    }
    if (best_hist) {
        std::string text = best_hist->text;
        if (text.size() > 500) text.resize(500);
        return {text, compute_confidence(evidence, need.keywords), Property::HISTORY};
    }
    auto filtered = filter_by_type(evidence,
        {ChunkType::HISTORY, ChunkType::TEMPORAL, ChunkType::GENERAL});
    auto segs = scored_segments(filtered, need.keywords, to_lower(need.entity),
        {"history", "origin", "founded", "century", "developed",
         "introduced", "evolved", "heritage"}, 4, 500);
    std::string text;
    for (auto& s : segs) { if (!text.empty()) text += " "; text += s; }
    if (text.empty()) text = "No history information found.";
    return {text, compute_confidence(filtered, need.keywords), Property::HISTORY};
}

Answer AnswerSynthesizer::synthesize_comparison(
    const InformationNeed& need, const std::vector<Evidence>& evidence) const
{
    // For comparison, collect segments mentioning any of the keywords
    auto segs = scored_segments(evidence, need.keywords, to_lower(need.entity),
        {"vs", "compare", "difference", "better", "worse", "more", "less",
         "affordable", "expensive", "cheaper"}, 6, 600);
    std::string text;
    for (auto& s : segs) { if (!text.empty()) text += " "; text += s; }
    if (text.empty()) text = "No comparison information found.";
    return {text, compute_confidence(evidence, need.keywords), Property::COMPARISON};
}

// ── Main dispatch ──

Answer AnswerSynthesizer::synthesize(
    const InformationNeed& need, const std::vector<Evidence>& evidence) const
{
    if (evidence.empty())
        return {"No relevant information found.", 0.0, need.property, {}};

    Answer answer;

    // Dispatch by property first, then refine by form
    switch (need.property) {
        case Property::LOCATION:    answer = synthesize_location(need, evidence); break;
        case Property::DEFINITION:  answer = synthesize_definition(need, evidence); break;
        case Property::TIME:        answer = synthesize_temporal(need, evidence); break;
        case Property::ADVANTAGES:  answer = synthesize_advantages(need, evidence); break;
        case Property::LIMITATIONS: answer = synthesize_limitations(need, evidence); break;
        case Property::USAGE:       answer = synthesize_usage(need, evidence); break;
        case Property::HISTORY:     answer = synthesize_history(need, evidence); break;
        case Property::COMPARISON:  answer = synthesize_comparison(need, evidence); break;
        default:
            switch (need.form) {
                case AnswerForm::EXPLANATION: answer = synthesize_explanation(need, evidence); break;
                case AnswerForm::LIST:        answer = synthesize_list(need, evidence); break;
                case AnswerForm::SHORT_FACT:  answer = synthesize_location(need, evidence); break;
                default:                      answer = synthesize_general(need, evidence); break;
            }
            break;
    }

    // Apply scope: set scope on answer and truncate text to scope limits
    answer.scope = need.scope;
    size_t max_chars = max_answer_chars(need.scope);
    if (answer.text.size() > max_chars) {
        // Truncate at sentence boundary near the limit
        size_t cut = answer.text.rfind('.', max_chars);
        if (cut != std::string::npos && cut > max_chars / 2)
            answer.text.resize(cut + 1);
        else
            answer.text.resize(max_chars);
    }

    // Collect unique source doc IDs from evidence
    std::unordered_set<uint32_t> seen;
    for (auto& e : evidence) {
        if (seen.insert(e.docId).second)
            answer.sources.push_back(e.docId);
    }
    return answer;
}
