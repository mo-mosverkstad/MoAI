#include "summarizer.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <sstream>
#include <map>

static const std::unordered_set<std::string>& stop_words() {
    static const std::unordered_set<std::string> sw = {
        "a", "an", "the", "is", "are", "was", "were", "be", "been", "being",
        "have", "has", "had", "do", "does", "did", "will", "would", "shall",
        "should", "may", "might", "must", "can", "could",
        "i", "me", "my", "we", "our", "you", "your", "he", "she", "it",
        "they", "them", "their", "this", "that", "these", "those",
        "what", "which", "who", "whom", "how", "when", "where", "why",
        "not", "no", "nor", "but", "and", "or", "if", "then", "so",
        "at", "by", "for", "with", "about", "from", "to", "in", "on",
        "of", "as", "into", "through", "during", "before", "after",
        "above", "below", "between", "out", "off", "over", "under",
        "again", "further", "once", "here", "there", "all", "each",
        "every", "both", "few", "more", "most", "other", "some", "such",
        "than", "too", "very", "just", "also"
    };
    return sw;
}

static std::vector<std::string> filter_query_tokens(
    const std::vector<std::string>& tokens)
{
    auto& sw = stop_words();
    std::vector<std::string> filtered;
    for (auto& t : tokens) {
        if (t.size() > 1 && sw.find(t) == sw.end())
            filtered.push_back(t);
    }
    return filtered;
}

static bool contains_word(const std::string& text_lower,
                           const std::string& word) {
    size_t pos = 0;
    while ((pos = text_lower.find(word, pos)) != std::string::npos) {
        bool left_ok = (pos == 0) ||
            !std::isalnum(static_cast<unsigned char>(text_lower[pos - 1]));
        bool right_ok = (pos + word.size() >= text_lower.size()) ||
            !std::isalnum(static_cast<unsigned char>(text_lower[pos + word.size()]));
        if (left_ok && right_ok) return true;
        pos++;
    }
    return false;
}

std::vector<std::string> Summarizer::split_sentences(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;

    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        cur.push_back(c);

        if (c == '.' || c == '!' || c == '?') {
            size_t start = 0;
            while (start < cur.size() &&
                   (cur[start] == ' ' || cur[start] == '\n' || cur[start] == '\r'))
                start++;
            std::string trimmed = cur.substr(start);
            if (trimmed.size() > 15)
                out.push_back(trimmed);
            cur.clear();
        }
    }
    if (!cur.empty()) {
        size_t start = 0;
        while (start < cur.size() &&
               (cur[start] == ' ' || cur[start] == '\n' || cur[start] == '\r'))
            start++;
        std::string trimmed = cur.substr(start);
        if (trimmed.size() > 15)
            out.push_back(trimmed);
    }
    return out;
}

double Summarizer::score_sentence(const std::string& sent,
                                   const std::vector<std::string>& query_tokens) {
    std::string lower;
    lower.reserve(sent.size());
    for (char c : sent)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    double score = 0.0;
    std::unordered_set<std::string> matched;

    for (auto& q : query_tokens) {
        if (contains_word(lower, q) && matched.find(q) == matched.end()) {
            score += 3.0;
            matched.insert(q);
        }
    }

    // Strong bonus for matching multiple query terms
    if (matched.size() > 1)
        score += static_cast<double>(matched.size()) * 3.0;

    // Slight preference for moderate-length sentences
    if (sent.size() > 30 && sent.size() < 300)
        score += 0.5;

    // Penalize headers and list items
    if (!sent.empty() && sent[0] == '#')
        score -= 2.0;
    if (!sent.empty() && sent[0] == '-')
        score -= 1.0;

    return score;
}

std::string Summarizer::summarize(
    const std::vector<std::pair<uint32_t, double>>& docs,
    const std::unordered_map<uint32_t, std::string>& doc_text,
    const std::vector<std::string>& query_tokens,
    size_t max_sentences)
{
    if (docs.empty()) return "No relevant information found.";

    auto filtered_tokens = filter_query_tokens(query_tokens);
    if (filtered_tokens.empty())
        return "No relevant information found for this query.";

    double top_score = docs[0].second;
    double threshold = top_score * 0.3;

    // Collect scored sentences per doc
    struct ScoredSent {
        double score;
        std::string text;
        uint32_t doc_id;
    };

    // Per-doc sentence lists, sorted by score
    std::map<uint32_t, std::vector<ScoredSent>> per_doc;
    size_t doc_count = 0;

    for (size_t i = 0; i < docs.size() && doc_count < 3; i++) {
        if (i > 0 && docs[i].second < threshold) continue;

        uint32_t d = docs[i].first;
        auto it = doc_text.find(d);
        if (it == doc_text.end()) continue;

        doc_count++;
        auto sents = split_sentences(it->second);
        for (auto& s : sents) {
            double sc = score_sentence(s, filtered_tokens);
            if (sc < 1.0) continue;
            per_doc[d].push_back({sc, s, d});
        }
    }

    // Sort each doc's sentences by score
    for (auto& [d, sents] : per_doc) {
        std::sort(sents.begin(), sents.end(),
                  [](auto& a, auto& b) { return a.score > b.score; });
    }

    // Select sentences: present each doc's content as a coherent block,
    // ordered by doc ranking (highest-scored doc first)
    size_t max_per_doc = std::max<size_t>(1,
        (max_sentences + per_doc.size() - 1) / std::max<size_t>(per_doc.size(), 1));

    std::vector<std::string> selected;

    // Iterate docs in their original ranking order
    for (size_t i = 0; i < docs.size() && selected.size() < max_sentences; i++) {
        uint32_t d = docs[i].first;
        auto it = per_doc.find(d);
        if (it == per_doc.end()) continue;

        size_t count = 0;
        for (auto& ss : it->second) {
            if (selected.size() >= max_sentences) break;
            if (count >= max_per_doc) break;

            // Dedup
            bool dup = false;
            for (auto& existing : selected) {
                if (ss.text.size() > 20 &&
                    existing.find(ss.text.substr(0, 20)) != std::string::npos) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                selected.push_back(ss.text);
                count++;
            }
        }
    }

    if (selected.empty()) return "No relevant information found.";

    std::string summary;
    for (auto& s : selected) {
        if (!summary.empty()) summary += " ";
        summary += s;
    }
    return summary;
}
