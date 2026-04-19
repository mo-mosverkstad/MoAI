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

// Filter evidence by preferred chunk types
static std::vector<Evidence> filter_by_type(
    const std::vector<Evidence>& evidence,
    const std::vector<ChunkType>& preferred)
{
    std::vector<Evidence> filtered;
    for (auto& e : evidence) {
        for (auto t : preferred) {
            if (e.type == t) { filtered.push_back(e); break; }
        }
    }
    return filtered.empty() ? evidence : filtered;
}

static void push_segment(std::vector<std::string>& out, const std::string& seg, size_t min_len = 15) {
    size_t start = seg.find_first_not_of(" \t\r\n-");
    if (start != std::string::npos) {
        std::string trimmed = seg.substr(start);
        if (trimmed.size() >= min_len) out.push_back(trimmed);
    }
}

// Split text into sentence-like segments (handles both prose and bullet lists)
static std::vector<std::string> split_into_segments(const std::string& text, size_t min_len = 15) {
    std::vector<std::string> segments;
    std::istringstream stream(text);
    std::string line;
    std::string cur;

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

// ── Shared methods ──

std::vector<std::string> AnswerSynthesizer::extract_sentences(
    const std::string& text,
    const std::vector<std::string>& keywords,
    size_t max_count) const
{
    std::vector<std::string> sentences;
    std::string cur;
    for (char c : text) {
        cur.push_back(c);
        if (c == '.' || c == '!' || c == '?') {
            size_t start = cur.find_first_not_of(" \t\r\n");
            if (start != std::string::npos) {
                std::string trimmed = cur.substr(start);
                if (trimmed.size() > 15) sentences.push_back(trimmed);
            }
            cur.clear();
        }
    }
    if (!cur.empty()) {
        size_t start = cur.find_first_not_of(" \t\r\n");
        if (start != std::string::npos && cur.size() - start > 15)
            sentences.push_back(cur.substr(start));
    }

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
            if (contains_word(lower, k)) {
                sc += 2.0;
            } else {
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

// ── Per-type synthesizers ──

Answer AnswerSynthesizer::synthesize_location(
    const QueryAnalysis& qa,
    const std::vector<Evidence>& evidence) const
{
    auto filtered = filter_by_type(evidence, {ChunkType::LOCATION, ChunkType::GENERAL});

    std::string text;
    for (auto& e : filtered) {
        auto sents = extract_sentences(e.text, qa.keywords, 2);
        for (auto& s : sents) {
            if (!text.empty()) text += " ";
            text += s;
        }
        if (text.size() > 300) break;
    }

    if (text.empty()) text = "Location information not found.";
    return {text, compute_confidence(filtered, qa.keywords), AnswerType::LOCATION};
}

Answer AnswerSynthesizer::synthesize_definition(
    const QueryAnalysis& qa,
    const std::vector<Evidence>& evidence) const
{
    auto filtered = filter_by_type(evidence, {ChunkType::DEFINITION, ChunkType::GENERAL});

    std::string text;
    for (auto& e : filtered) {
        auto sents = extract_sentences(e.text, qa.keywords, 3);
        for (auto& s : sents) {
            if (!text.empty()) text += " ";
            text += s;
        }
        if (text.size() > 500) break;
    }

    if (text.empty()) text = "No definition found.";
    return {text, compute_confidence(filtered, qa.keywords), AnswerType::DEFINITION};
}

Answer AnswerSynthesizer::synthesize_person(
    const QueryAnalysis& qa,
    const std::vector<Evidence>& evidence) const
{
    std::string entity_lower = to_lower(qa.mainEntity);

    struct Candidate { double score; std::string text; };
    std::vector<Candidate> all_candidates;

    for (auto& e : evidence) {
        auto segs = split_into_segments(e.text);
        for (auto& s : segs) {
            std::string lower = to_lower(s);
            double sc = 0.0;
            for (auto& k : qa.keywords) {
                if (contains_word(lower, k)) {
                    sc += 3.0;
                } else {
                    std::string stem = (k.size() > 4) ? k.substr(0, k.size() - 2) : k;
                    if (lower.find(stem) != std::string::npos) sc += 1.5;
                }
            }
            if (!entity_lower.empty() && contains_word(lower, entity_lower)) sc += 5.0;
            if (s.size() > 30 && s.size() < 300) sc += 0.5;
            if (!s.empty() && s[0] == '#') sc -= 2.0;
            if (sc > 0.0) {
                Candidate c;
                c.score = sc;
                c.text = s;
                all_candidates.push_back(std::move(c));
            }
        }
    }

    std::sort(all_candidates.begin(), all_candidates.end(),
              [](auto& a, auto& b) { return a.score > b.score; });

    std::string text;
    for (size_t i = 0; i < 3 && i < all_candidates.size(); i++) {
        if (!text.empty()) text += " ";
        text += all_candidates[i].text;
        if (text.size() > 400) break;
    }

    if (text.empty()) text = "Person information not found.";
    return {text, compute_confidence(evidence, qa.keywords), AnswerType::PERSON_PROFILE};
}

Answer AnswerSynthesizer::synthesize_temporal(
    const QueryAnalysis& qa,
    const std::vector<Evidence>& evidence) const
{
    struct Candidate { double score; std::string text; };
    std::vector<Candidate> all_candidates;
    static const std::regex yr_re("\\b(1[0-9]{3}|20[0-9]{2})\\b");
    std::string entity_lower = to_lower(qa.mainEntity);

    for (auto& e : evidence) {
        auto segs = split_into_segments(e.text);
        for (auto& s : segs) {
            std::string lower = to_lower(s);
            double sc = 0.0;
            for (auto& k : qa.keywords) {
                if (contains_word(lower, k)) {
                    sc += 2.0;
                } else {
                    std::string stem = (k.size() > 4) ? k.substr(0, k.size() - 2) : k;
                    if (lower.find(stem) != std::string::npos) sc += 1.5;
                }
            }
            if (std::regex_search(lower, yr_re)) sc += 6.0;
            if (!entity_lower.empty() && contains_word(lower, entity_lower)) sc += 4.0;
            if (s.size() > 30 && s.size() < 300) sc += 0.5;
            if (!s.empty() && s[0] == '#') sc -= 2.0;
            if (sc > 0.0) {
                Candidate c;
                c.score = sc;
                c.text = s;
                all_candidates.push_back(std::move(c));
            }
        }
    }

    std::sort(all_candidates.begin(), all_candidates.end(),
              [](auto& a, auto& b) { return a.score > b.score; });

    std::string text;
    for (size_t i = 0; i < 3 && i < all_candidates.size(); i++) {
        if (!text.empty()) text += " ";
        text += all_candidates[i].text;
        if (text.size() > 300) break;
    }

    if (text.empty()) text = "Temporal information not found.";
    return {text, compute_confidence(evidence, qa.keywords), AnswerType::TEMPORAL};
}

Answer AnswerSynthesizer::synthesize_summary(
    const QueryAnalysis& qa,
    const std::vector<Evidence>& evidence) const
{
    std::string text;
    for (auto& e : evidence) {
        auto sents = extract_sentences(e.text, qa.keywords, 2);
        for (auto& s : sents) {
            if (!text.empty()) text += " ";
            text += s;
        }
        if (text.size() > 600) break;
    }

    if (text.empty()) text = "No relevant information found.";
    return {text, compute_confidence(evidence, qa.keywords), AnswerType::SUMMARY};
}

Answer AnswerSynthesizer::synthesize(
    const QueryAnalysis& analysis,
    const std::vector<Evidence>& evidence) const
{
    if (evidence.empty())
        return {"No relevant information found.", 0.0, analysis.answerType};

    switch (analysis.answerType) {
        case AnswerType::LOCATION:       return synthesize_location(analysis, evidence);
        case AnswerType::DEFINITION:     return synthesize_definition(analysis, evidence);
        case AnswerType::PERSON_PROFILE: return synthesize_person(analysis, evidence);
        case AnswerType::TEMPORAL:       return synthesize_temporal(analysis, evidence);
        case AnswerType::PROCEDURE:      return synthesize_summary(analysis, evidence);
        case AnswerType::COMPARISON:     return synthesize_summary(analysis, evidence);
        case AnswerType::SUMMARY:        return synthesize_summary(analysis, evidence);
    }
    return synthesize_summary(analysis, evidence);
}
